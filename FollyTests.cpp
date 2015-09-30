#include "FollyTests.h"

#include <stdint.h>

#include "glog/Logging.h"
#include "folly/Format.h"
#include "folly/json.h"
#include "folly/experimental/FunctionScheduler.h"

namespace libslack
{
    void FollyTests::test(const std::string& root_fs) {
        LOG(TESTS) << SEP;

        LOG(TESTS)
            << folly::sformat("hello, my name is {}, version, {}",
                                "superman",
                                3.1);

        const int M = 1000000;
        const int B = 1000 * M;

        LOG(TESTS)
            << folly::sformat("million, '{0:,d}', billion, '{1:,d}'", M, B);

        // int to string:
        const int x = folly::to<int>("101");
        LOG(TESTS) << "x, " << x;
        assert(x == 101);

        // string to int
        const std::string s = folly::to<std::string>(101);
        LOG(TESTS) << "s, " << s;
        assert("101" == s);

        folly::dynamic array = {"superman", "batman", 1, 2, 3};
        LOG(TESTS) << "array = " << folly::toPrettyJson(array);
        assert(array[0] == "superman");
        assert(array[1] == "batman");
        assert(array[2] == 1);
        assert(array.size() == 5);
        assert(array[0].isString());

        folly::dynamic map = folly::dynamic::object;
        map["one"] = 1;
        map["two"] = map["one"] * 2;
        map["four"] = map["two"] * 2;
        LOG(TESTS) << "map = " << folly::toPrettyJson(map);
        assert(map["one"] == 1);
        assert(map["two"] == 2);
        assert(map["four"] == 4);

        auto* o = map.get_ptr("five");
        CHECK(!o);

        if (auto* oo = map.get_ptr("four")) {
            CHECK(oo && oo->isNumber());
        }
        else {
            CHECK(false);
        }

        #if 0 // no folly::File / folly::FileUtil
          const std::string helloMsg = "batman says hello";
          const std::string helloFilename = root_fs + "/hello_libslack.txt";
          auto helloFile = folly::File{helloFilename.c_str(),
                                                    O_RDWR | O_CREAT};
          folly::writeFull(helloFile.fd(), helloMsg.c_str(), helloMsg.size());
          helloFile.close();
          helloFile = folly::File{helloFilename.c_str()};
          std::string msg;
          folly::readFile(helloFilename.c_str(), msg);
          CHECK_EQ(msg, helloMsg);
        #endif

        LOG(TESTS) << SEP;

        {
            unsigned int C = 0;
            const unsigned int N = 3;
            const std::chrono::milliseconds dt(100);

            folly::FunctionScheduler scheduler;

            auto tStart = std::chrono::steady_clock::now();
            auto tEnd = tStart;

            LOG(TESTS) << "START, C, " << C;

            auto function = [&] {
                C++;
                LOG(TESTS) << "UPDATE, C, " << C;
                tEnd = std::chrono::steady_clock::now();
            };

            scheduler.addFunction(function,
                                  dt,
                                  "scheduler",
                                  std::chrono::milliseconds(0));// milliseconds
            scheduler.start();

            std::this_thread::sleep_for(std::chrono::milliseconds((N+1)*dt.count()));

            auto total_duration =
                std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();

            LOG(TESTS)
                << "DONE, C, " << C
                << ", DURATION, " << total_duration
                << ", N, " << N
                << ", dt, " << dt.count();

            CHECK(C > 0) << "should be greater than 0";
            CHECK(total_duration >= (N * dt.count())) << "less than expected C, " << C;
        }

        {
            std::map<std::string, std::string> m;
            m.insert(std::make_pair("y", "10"));
            m.insert(std::make_pair("y", "20"));
            CHECK(m["y"] == "10");

            m["x"] = "10";
            m["x"] = "20";
            CHECK(m["x"] == "20");
        }
    }
}


