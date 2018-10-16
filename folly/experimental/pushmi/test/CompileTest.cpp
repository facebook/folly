#include "pushmi.h"

using namespace pushmi::aliases;

using namespace std::literals;

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) {__VA_ARGS__}
#else
#define MAKE(x) make_ ## x
#endif

void none_test() {
  auto out0 = pushmi::MAKE(none)();
  auto out1 = pushmi::MAKE(none)(pushmi::abortEF{});
  auto out2 = pushmi::MAKE(none)(pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out3 = pushmi::MAKE(none)([](auto e) noexcept{ e.get(); });
  auto out5 = pushmi::MAKE(none)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(none)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(none)(out0);
  auto proxy2 = pushmi::MAKE(none)(out0, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(none)(
      out0, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(none)(out0, [](auto d, auto e)noexcept {
    d.error(e.get());
  });
  auto proxy5 = pushmi::MAKE(none)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept{},
        [](Out0&, auto e) noexcept{ e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(none)(
      out0,
      pushmi::on_done([](Out0&) { }));

  std::promise<void> p0;
  auto promise0 = pushmi::MAKE(none)(std::move(p0));
  promise0.done();

  std::promise<void> p1;

  auto any0 = pushmi::any_none<>(std::move(p1));
  auto any1 = pushmi::any_none<>(std::move(promise0));
  auto any2 = pushmi::any_none<>(out0);
  auto any3 = pushmi::any_none<>(proxy0);
}

void sender_test(){
  auto in0 = pushmi::MAKE(sender)();
  auto in1 = pushmi::MAKE(sender)(pushmi::ignoreSF{});
  auto in3 = pushmi::MAKE(sender)([&](auto out){
    in0.submit(pushmi::MAKE(none)(std::move(out), [](auto d, auto e) noexcept {
      pushmi::set_error(d, e);
    }));
  });
  in3.submit(pushmi::MAKE(none)());

  std::promise<void> p0;
  auto promise0 = pushmi::MAKE(none)(std::move(p0));
  in0 | ep::submit(std::move(promise0));

  auto out0 = pushmi::MAKE(none)([](auto e) noexcept {  });
  auto out1 = pushmi::MAKE(none)(out0, [](auto d, auto e) noexcept {});
  out1.error(std::exception_ptr{});
  in3.submit(out1);

  auto any0 = pushmi::any_sender<>(in0);
}

void single_test() {
  auto out0 = pushmi::MAKE(single)();
  auto out1 = pushmi::MAKE(single)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(single)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(single)(pushmi::ignoreVF{}, pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(single)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(single)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e)noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(single)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out7 = pushmi::MAKE(single)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(single)(out0);
  auto proxy1 = pushmi::MAKE(single)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(single)(out0, pushmi::passDVF{}, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(single)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(single)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(single)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(single)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(single)(
      out0,
      pushmi::on_done([](Out0&) { }));

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(single)(std::move(p0));
  promise0.value(0);

  std::promise<int> p1;

  auto any0 = pushmi::any_single<int>(std::move(p1));
  auto any1 = pushmi::any_single<int>(std::move(promise0));
  auto any2 = pushmi::any_single<int>(out0);
  auto any3 = pushmi::any_single<int>(proxy0);
}

void many_test() {
  auto out0 = pushmi::MAKE(many)();
  auto out1 = pushmi::MAKE(many)(pushmi::ignoreNF{});
  auto out2 = pushmi::MAKE(many)(pushmi::ignoreNF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(many)(pushmi::ignoreNF{}, pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(many)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(many)(
      pushmi::on_next([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e)noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(many)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out7 = pushmi::MAKE(many)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(many)(out0);
  auto proxy1 = pushmi::MAKE(many)(out0, pushmi::passDNXF{});
  auto proxy2 = pushmi::MAKE(many)(out0, pushmi::passDNXF{}, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(many)(
      out0, pushmi::passDNXF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(many)(out0, [](auto d, auto v) {
    pushmi::set_next(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(many)(
      out0,
      pushmi::on_next([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(many)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(many)(
      out0,
      pushmi::on_done([](Out0&) { }));

  auto any0 = pushmi::any_many<int>(out0);
  auto any1 = pushmi::any_many<int>(proxy0);
}

void single_sender_test(){
  auto in0 = pushmi::MAKE(single_sender)();
  auto in1 = pushmi::MAKE(single_sender)(pushmi::ignoreSF{});
  auto in3 = pushmi::MAKE(single_sender)([&](auto out){
    in0.submit(pushmi::MAKE(single)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  });

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(single)(std::move(p0));
  in0 | ep::submit(std::move(promise0));

  auto out0 = pushmi::MAKE(single)();
  auto out1 = pushmi::MAKE(single)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_single_sender<int>(in0);
}

void many_sender_test(){
  auto in0 = pushmi::MAKE(many_sender)();
  auto in1 = pushmi::MAKE(many_sender)(pushmi::ignoreSF{});
  auto in3 = pushmi::MAKE(many_sender)([&](auto out){
    in0.submit(pushmi::MAKE(many)(std::move(out),
      pushmi::on_next([](auto d, int v){ pushmi::set_next(d, v); })
    ));
  });

  auto out0 = pushmi::MAKE(many)();
  auto out1 = pushmi::MAKE(many)(out0, pushmi::on_next([](auto d, int v){
    pushmi::set_next(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_many_sender<int>(in0);
}

void time_single_sender_test(){
  auto in0 = pushmi::MAKE(time_single_sender)();
  auto in1 = pushmi::MAKE(time_single_sender)(pushmi::ignoreSF{});
  auto in3 = pushmi::MAKE(time_single_sender)([&](auto tp, auto out){
    in0.submit(tp, pushmi::MAKE(single)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  });
  auto in4 = pushmi::MAKE(time_single_sender)(pushmi::ignoreSF{}, pushmi::systemNowF{});

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(single)(std::move(p0));
  in0.submit(in0.now(), std::move(promise0));

  auto out0 = pushmi::MAKE(single)();
  auto out1 = pushmi::MAKE(single)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(in0.now(), out1);

  auto any0 = pushmi::any_time_single_sender<int>(in0);

  in3 | op::submit();
  in3 | op::submit_at(in3.now() + 1s);
  in3 | op::submit_after(1s);

#if 0
  auto tr = v::trampoline();
  tr |
    op::transform([]<class TR>(TR tr) {
      return v::get_now(tr);
    }) |
    // op::submit(v::MAKE(single){});
    op::get<std::chrono::system_clock::time_point>();

  std::vector<std::string> times;
  auto push = [&](int time) {
    return v::on_value([&, time](auto) { times.push_back(std::to_string(time)); });
  };

  auto nt = v::new_thread();

  auto out = v::any_single<v::any_time_executor_ref<std::exception_ptr, std::chrono::system_clock::time_point>>{v::MAKE(single){}};
  (v::any_time_executor_ref{nt}).submit(v::get_now(nt), v::MAKE(single){});

  nt |
    op::transform([&]<class NT>(NT nt){
      // auto out = v::any_single<v::any_time_executor_ref<std::exception_ptr, std::chrono::system_clock::time_point>>{v::MAKE(single){}};
      // nt.submit(v::get_now(nt), std::move(out));
      // nt.submit(v::get_now(nt), v::MAKE(single){});
      // nt | op::submit_at(v::get_now(nt), std::move(out));
      // nt |
      // op::submit(v::MAKE(single){});
      // op::submit_at(nt | ep::get_now(), v::on_value{[](auto){}}) |
      // op::submit_at(nt | ep::get_now(), v::on_value{[](auto){}}) |
      // op::submit_after(20ms, v::MAKE(single){}) ;//|
      // op::submit_after(20ms, v::on_value{[](auto){}}) |
      // op::submit_after(40ms, push(42));
      return v::get_now(nt);
    }) |
   // op::submit(v::MAKE(single){});
   op::blocking_submit(v::MAKE(single){});
    // op::get<decltype(v::get_now(nt))>();
    // op::get<std::chrono::system_clock::time_point>();

  auto ref = v::any_time_executor_ref<std::exception_ptr, std::chrono::system_clock::time_point>{nt};
#endif
}

void flow_single_test() {
  auto out0 = pushmi::MAKE(flow_single)();
  auto out1 = pushmi::MAKE(flow_single)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(flow_single)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(flow_single)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(flow_single)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(flow_single)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept{},
        [](auto e) noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(flow_single)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept{ e.get(); }
      ));
  auto out7 = pushmi::MAKE(flow_single)(
      pushmi::on_done([]() {  }));

  auto out8 =
      pushmi::MAKE(flow_single)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{},
        pushmi::ignoreStrtF{});

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(flow_single)(out0);
  auto proxy1 = pushmi::MAKE(flow_single)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(flow_single)(out0, pushmi::passDVF{}, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(flow_single)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(flow_single)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(flow_single)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(flow_single)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(flow_single)(
      out0,
      pushmi::on_done([](Out0&) { }));

  auto proxy8 = pushmi::MAKE(flow_single)(out0,
    pushmi::passDVF{},
    pushmi::passDEF{},
    pushmi::passDDF{});

  auto any2 = pushmi::any_flow_single<int>(out0);
  auto any3 = pushmi::any_flow_single<int>(proxy0);
}

void flow_single_sender_test(){
  auto in0 = pushmi::MAKE(flow_single_sender)();
  auto in1 = pushmi::MAKE(flow_single_sender)(pushmi::ignoreSF{});
  auto in3 = pushmi::MAKE(flow_single_sender)([&](auto out){
    in0.submit(pushmi::MAKE(flow_single)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  });

  auto out0 = pushmi::MAKE(flow_single)();
  auto out1 = pushmi::MAKE(flow_single)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_flow_single_sender<int>(in0);
}
