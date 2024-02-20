/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/json/json.h>

#include <folly/Benchmark.h>

#include <fstream>
#include <streambuf>

using namespace folly;

static constexpr folly::StringPiece kLargeAsciiString =
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
    "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk";

static constexpr folly::StringPiece kLargeNonAsciiString =
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
    "qwerty \xc2\x80 \xef\xbf\xbf poiuy";

static constexpr folly::StringPiece kLargeAsciiStringWithSpecialChars =
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>"
    "<script>foo%@bar.com</script>";

BENCHMARK(jsonSerialize, iters) {
  const dynamic obj = kLargeNonAsciiString;

  folly::json::serialization_opts opts;
  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(obj, opts);
  }
}

BENCHMARK(jsonSerializeWithNonAsciiEncoding, iters) {
  const dynamic obj = kLargeNonAsciiString;

  folly::json::serialization_opts opts;
  opts.encode_non_ascii = true;

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(obj, opts);
  }
}

BENCHMARK(jsonSerializeWithUtf8Validation, iters) {
  const dynamic obj = kLargeNonAsciiString;

  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(obj, opts);
  }
}

BENCHMARK(jsonSerializeAsciiWithUtf8Validation, iters) {
  const dynamic obj = kLargeAsciiString;

  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(obj, opts);
  }
}

BENCHMARK(jsonSerializeWithExtraUnicodeEscapes, iters) {
  const dynamic obj = kLargeAsciiStringWithSpecialChars;

  folly::json::serialization_opts opts;
  opts.extra_ascii_to_escape_bitmap =
      folly::json::buildExtraAsciiToEscapeBitmap("<%@");

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(obj, opts);
  }
}

BENCHMARK(parseSmallStringWithUtf, iters) {
  for (size_t i = 0; i < iters << 4; ++i) {
    parseJson("\"I \\u2665 UTF-8 thjasdhkjh blah blah blah\"");
  }
}

BENCHMARK(parseNormalString, iters) {
  for (size_t i = 0; i < iters << 4; ++i) {
    parseJson("\"akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk\"");
  }
}

BENCHMARK(parseBigString, iters) {
  const auto json = folly::to<std::string>('"', kLargeAsciiString, '"');

  for (size_t i = 0; i < iters; ++i) {
    parseJson(json);
  }
}

BENCHMARK(toJson, iters) {
  dynamic something = parseJson(
      "{\"old_value\":40,\"changed\":true,\"opened\":false,\"foo\":[1,2,3,4,5,6]}");

  for (size_t i = 0; i < iters; i++) {
    toJson(something);
  }
}

// We are using a small sample file (https://json.org/example.html),
// but for true benchmarking a bigger JSON file is more interesting, like from
// https://github.com/simdjson/simdjson/tree/master/jsonexamples or
// https://github.com/chadaustin/Web-Benchmarks/tree/master/json/testdata
static constexpr StringPiece kJsonBenchmarkString = R"END(
{"web-app": {
  "servlet": [
    {
      "servlet-name": "cofaxCDS",
      "servlet-class": "org.cofax.cds.CDSServlet",
      "init-param": {
        "configGlossary:installationAt": "Philadelphia, PA",
        "configGlossary:adminEmail": "ksm@pobox.com",
        "configGlossary:poweredBy": "Cofax",
        "configGlossary:poweredByIcon": "/images/cofax.gif",
        "configGlossary:staticPath": "/content/static",
        "templateProcessorClass": "org.cofax.WysiwygTemplate",
        "templateLoaderClass": "org.cofax.FilesTemplateLoader",
        "templatePath": "templates",
        "templateOverridePath": "",
        "defaultListTemplate": "listTemplate.htm",
        "defaultFileTemplate": "articleTemplate.htm",
        "useJSP": false,
        "jspListTemplate": "listTemplate.jsp",
        "jspFileTemplate": "articleTemplate.jsp",
        "cachePackageTagsTrack": 200,
        "cachePackageTagsStore": 200,
        "cachePackageTagsRefresh": 60,
        "cacheTemplatesTrack": 100,
        "cacheTemplatesStore": 50,
        "cacheTemplatesRefresh": 15,
        "cachePagesTrack": 200,
        "cachePagesStore": 100,
        "cachePagesRefresh": 10,
        "cachePagesDirtyRead": 10,
        "searchEngineListTemplate": "forSearchEnginesList.htm",
        "searchEngineFileTemplate": "forSearchEngines.htm",
        "searchEngineRobotsDb": "WEB-INF/robots.db",
        "useDataStore": true,
        "dataStoreClass": "org.cofax.SqlDataStore",
        "redirectionClass": "org.cofax.SqlRedirection",
        "dataStoreName": "cofax",
        "dataStoreDriver": "com.microsoft.jdbc.sqlserver.SQLServerDriver",
        "dataStoreUrl": "jdbc:microsoft:sqlserver://LOCALHOST:1433;DatabaseName=goon",
        "dataStoreUser": "sa",
        "dataStorePassword": "dataStoreTestQuery",
        "dataStoreTestQuery": "SET NOCOUNT ON;select test='test';",
        "dataStoreLogFile": "/usr/local/tomcat/logs/datastore.log",
        "dataStoreInitConns": 10,
        "dataStoreMaxConns": 100,
        "dataStoreConnUsageLimit": 100,
        "dataStoreLogLevel": "debug",
        "maxUrlLength": 500}},
    {
      "servlet-name": "cofaxEmail",
      "servlet-class": "org.cofax.cds.EmailServlet",
      "init-param": {
      "mailHost": "mail1",
      "mailHostOverride": "mail2"}},
    {
      "servlet-name": "cofaxAdmin",
      "servlet-class": "org.cofax.cds.AdminServlet"},
    {
      "servlet-name": "fileServlet",
      "servlet-class": "org.cofax.cds.FileServlet"},
    {
      "servlet-name": "cofaxTools",
      "servlet-class": "org.cofax.cms.CofaxToolsServlet",
      "init-param": {
        "templatePath": "toolstemplates/",
        "log": 1,
        "logLocation": "/usr/local/tomcat/logs/CofaxTools.log",
        "logMaxSize": "",
        "dataLog": 1,
        "dataLogLocation": "/usr/local/tomcat/logs/dataLog.log",
        "dataLogMaxSize": "",
        "removePageCache": "/content/admin/remove?cache=pages&id=",
        "removeTemplateCache": "/content/admin/remove?cache=templates&id=",
        "fileTransferFolder": "/usr/local/tomcat/webapps/content/fileTransferFolder",
        "lookInContext": 1,
        "adminGroupID": 4,
        "betaServer": true}}],
  "servlet-mapping": {
    "cofaxCDS": "/",
    "cofaxEmail": "/cofaxutil/aemail/*",
    "cofaxAdmin": "/admin/*",
    "fileServlet": "/static/*",
    "cofaxTools": "/tools/*"},

  "taglib": {
    "taglib-uri": "cofax.tld",
    "taglib-location": "/WEB-INF/tlds/cofax.tld"}}}
)END";

BENCHMARK(PerfJson2Obj, iters) {
  for (size_t i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(parseJson(kJsonBenchmarkString));
  }
}

BENCHMARK(PerfObj2Json, iters) {
  BenchmarkSuspender s;
  dynamic parsed = parseJson(kJsonBenchmarkString);
  s.dismiss();

  for (size_t i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(toJson(parsed));
  }
}

// Benchmark results in a Macbook Pro 2015 (i7-4870HQ, 4th gen)
// ============================================================================
// folly/test/JsonBenchmark.cpp                   relative  time/iter   iters/s
// ============================================================================
// PerfJson2Obj                                              903.84us     1.11K
// PerfObj2Json                                              265.90us     3.76K

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
