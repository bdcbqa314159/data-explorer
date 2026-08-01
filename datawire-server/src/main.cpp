// datawire-server — M6 slice 3: a small synchronous HTTP/JSON server (Boost.Beast)
// over the MySQL-backed Db. Loopback-only by default; the terminal's SDK sync
// client (M7, over TLS) will talk to this.
//
//   GET  /health          -> {"status":"ok"}
//   GET  /series          -> ["ID", ...]
//   GET  /series/<id>      -> {"meta":{...},"observations":[{"date","value"},...]} | 404
//   POST /series          -> body = a series object; upserts; {"status":"stored"}
//
// ponytail: single-threaded, one connection at a time — plenty for a local,
// single-client terminal. Swap for an async/thread-pool accept loop if it ever
// serves many clients.

#include "db.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace json = boost::json;
using tcp = net::ip::tcp;
using namespace datawire;

namespace {

std::string env(const char* key, const char* fallback) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : std::string(fallback);
}

json::value seriesToJson(const Series& s) {
  json::array obs;
  for (const auto& o : s.observations) obs.push_back(json::object{{"date", o.date}, {"value", o.value}});
  return json::object{
      {"meta",
       json::object{{"id", s.meta.id},
                    {"title", s.meta.title},
                    {"unit", s.meta.unit},
                    {"frequency", s.meta.frequency},
                    {"seasonalAdj", s.meta.seasonalAdj},
                    {"asOf", s.meta.asOf},
                    {"sourceUrl", s.meta.sourceUrl},
                    {"source", s.meta.source}}},
      {"observations", obs}};
}

std::string jstr(const json::object& o, const char* k) {
  auto* v = o.if_contains(k);
  return (v && v->is_string()) ? std::string(v->as_string().c_str()) : std::string{};
}

Series seriesFromJson(const json::value& v) {
  Series s;
  const json::object& root = v.as_object();
  if (auto* m = root.if_contains("meta"); m && m->is_object()) {
    const json::object& meta = m->as_object();
    s.meta.id = jstr(meta, "id");
    s.meta.title = jstr(meta, "title");
    s.meta.unit = jstr(meta, "unit");
    s.meta.frequency = jstr(meta, "frequency");
    s.meta.seasonalAdj = jstr(meta, "seasonalAdj");
    s.meta.asOf = jstr(meta, "asOf");
    s.meta.sourceUrl = jstr(meta, "sourceUrl");
    s.meta.source = jstr(meta, "source");
  }
  if (auto* obs = root.if_contains("observations"); obs && obs->is_array())
    for (const auto& o : obs->as_array())
      if (o.is_object()) {
        const json::object& oo = o.as_object();
        auto* val = oo.if_contains("value");
        s.observations.push_back({jstr(oo, "date"), val ? val->to_number<double>() : 0.0});
      }
  return s;
}

http::response<http::string_body> route(const http::request<http::string_body>& req, server::Db& db) {
  auto reply = [&](http::status st, std::string body) {
    http::response<http::string_body> r{st, req.version()};
    r.set(http::field::server, "datawire-server");
    r.set(http::field::content_type, "application/json");
    r.keep_alive(req.keep_alive());
    r.body() = std::move(body);
    r.prepare_payload();
    return r;
  };

  const std::string target(req.target());
  try {
    if (req.method() == http::verb::get && target == "/health")
      return reply(http::status::ok, R"({"status":"ok"})");

    if (req.method() == http::verb::get && target == "/series")
      return reply(http::status::ok, json::serialize(json::value_from(db.listSeriesIds())));

    if (req.method() == http::verb::get && target.rfind("/series/", 0) == 0) {
      const std::string id = target.substr(std::string("/series/").size());
      const auto s = db.getSeries(id);
      if (!s) return reply(http::status::not_found, R"({"error":"not found"})");
      return reply(http::status::ok, json::serialize(seriesToJson(*s)));
    }

    if (req.method() == http::verb::post && target == "/series") {
      const Series s = seriesFromJson(json::parse(req.body()));
      if (s.meta.id.empty()) return reply(http::status::bad_request, R"({"error":"missing meta.id"})");
      db.upsertSeries(s);
      return reply(http::status::ok, R"({"status":"stored"})");
    }

    return reply(http::status::not_found, R"({"error":"no route"})");
  } catch (const std::exception& e) {
    return reply(http::status::internal_server_error, json::serialize(json::object{{"error", e.what()}}));
  }
}

// One TLS session: handshake, then serve HTTP requests over the encrypted stream.
void serve(ssl::stream<tcp::socket> stream, server::Db& db) {
  beast::error_code ec;
  stream.handshake(ssl::stream_base::server, ec);
  if (ec) return;  // failed/untrusted client handshake — drop it

  beast::flat_buffer buffer;
  for (;;) {
    http::request<http::string_body> req;
    http::read(stream, buffer, req, ec);
    if (ec == http::error::end_of_stream) break;
    if (ec) return;
    http::response<http::string_body> res = route(req, db);
    const bool keep = res.keep_alive();
    http::write(stream, res, ec);
    if (ec || !keep) break;
  }
  stream.shutdown(ec);  // TLS close-notify (ignore errors on teardown)
}

}  // namespace

int main() {
  try {
    server::Db db;  // connect + schema up front; fail fast if MySQL is down
    const unsigned short port = static_cast<unsigned short>(std::stoi(env("DATAWIRE_PORT", "8080")));
    const std::string cert = env("DATAWIRE_TLS_CERT", "certs/server.crt");
    const std::string key = env("DATAWIRE_TLS_KEY", "certs/server.key");

    // TLS 1.2+ context with the dev cert. `./gen-cert.sh` creates the files.
    ssl::context tls(ssl::context::tls_server);
    tls.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                    ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1 |
                    ssl::context::single_dh_use);
    tls.use_certificate_chain_file(cert);
    tls.use_private_key_file(key, ssl::context::pem);

    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), port));
    std::cout << "datawire-server listening on https://127.0.0.1:" << port << " (TLS)\n";

    for (;;) {
      tcp::socket socket(ioc);
      acceptor.accept(socket);
      serve(ssl::stream<tcp::socket>(std::move(socket), tls), db);
    }
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what()
              << "\n(MySQL running? `brew services start mysql`; cert present? `./gen-cert.sh`)\n";
    return 1;
  }
}
