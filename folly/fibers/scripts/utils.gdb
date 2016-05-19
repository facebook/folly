# Print given Fiber state
# arg0 folly::fibers::Fiber*
define print_folly_fiber_state
  set $fiber = (folly::fibers::Fiber*)$arg0
  if $fiber->state_ == folly::fibers::Fiber::INVALID
    printf "Invalid"
  end
  if $fiber->state_ == folly::fibers::Fiber::NOT_STARTED
    printf "Not started"
  end
  if $fiber->state_ == folly::fibers::Fiber::READY_TO_RUN
    printf "Ready to run"
  end
  if $fiber->state_ == folly::fibers::Fiber::RUNNING
    printf "Running"
  end
  if $fiber->state_ == folly::fibers::Fiber::AWAITING
    printf "Awaiting"
  end
  if $fiber->state_ == folly::fibers::Fiber::AWAITING_IMMEDIATE
    printf "Awaiting immediate"
  end
  if $fiber->state_ == folly::fibers::Fiber::YIELDED
    printf "Yielded"
  end
end

# Print given Fiber
# arg0 folly::fibers::Fiber*
define print_folly_fiber
  set $fiber = (folly::fibers::Fiber*)$arg0
  printf "  (folly::fibers::Fiber*)%p\n\n", $fiber

  printf "  State: "
  print_folly_fiber_state $fiber
  printf "\n"

  if $fiber->state_ != folly::fibers::Fiber::INVALID && \
     $fiber->state_ != folly::fibers::Fiber::NOT_STARTED && \
     $fiber->state_ != folly::fibers::Fiber::RUNNING
    printf "  Backtrace:\n"
    set $frameptr = ((uint64_t*)$fiber->fcontext_.context_)[6]
    set $k = 0
    while $frameptr != 0
      printf "    #%d at %p in ", $k, *((void**)($frameptr+8))
      set $k = $k + 1
      info symbol *((void**)($frameptr+8))
      set $frameptr = *((void**)($frameptr))
    end
  end
end

# Print given FiberManager
# arg0 folly::fibers::FiberManager*
define print_folly_fiber_manager
  set $fiberManager = (folly::fibers::FiberManager*)$arg0

  printf "  (folly::fibers::FiberManager*)%p\n\n", $fiberManager
  printf "  Fibers active: %d\n", $fiberManager->fibersActive_
  printf "  Fibers allocated: %d\n", $fiberManager->fibersAllocated_
  printf "  Fibers pool size: %d\n", $fiberManager->fibersPoolSize_
  printf "  Active fiber: (folly::fibers::Fiber*)%p\n", \
         $fiberManager->activeFiber_
  printf "  Current fiber: (folly::fibers::Fiber*)%p\n", \
         $fiberManager->currentFiber_

  set $all_fibers = &($fiberManager->allFibers_.data_.root_plus_size_.m_header)
  set $fiber_hook = $all_fibers->next_
  printf "\n  Active fibers:\n"
  while $fiber_hook != $all_fibers
    set $fiber = (folly::fibers::Fiber*) \
        ((int64_t)$fiber_hook - \
         (int64_t)&folly::fibers::Fiber::globalListHook_)
    if $fiber->state_ != folly::fibers::Fiber::INVALID
      printf "    (folly::fibers::Fiber*)%p   State: ", $fiber
      print_folly_fiber_state $fiber
      printf "\n"
    end
    set $fiber_hook = $fiber_hook->next_
  end
end

# Print global FiberManager map
define print_folly_fiber_manager_map
  set $global_cache=*(('folly::fibers::(anonymous namespace)::GlobalCache'**) \
      &'folly::fibers::(anonymous namespace)::GlobalCache::instance()::ret')
  printf "  Global FiberManager map has %d entries.\n", \
         $global_cache->map_->_M_h->_M_element_count

  set $item = $global_cache->map_->_M_h->_M_before_begin._M_nxt
  while $item != 0
    set $evb = ((folly::EventBase**)$item)[1]
    set $fiberManager = ((folly::fibers::FiberManager**)$item)[2]
    printf "    (folly::EventBase*)%p -> (folly::fibers::FiberManager*)%p\n", \
           $evb, $fiberManager

    set $item = $item->_M_nxt
  end
end
