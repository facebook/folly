<!-- This file is generated from internal wiki guide by folly/facebook/fibers-update-readme.sh. -->
<section class="dex_guide"><h1 class="dex_title">folly::fibers</h1><section class="dex_document"><h1></h1><p class="dex_introduction">folly::fibers is an async C++ framework, which uses fibers for parallelism.</p><h2 id="overview">Overview <a href="#overview" class="headerLink">#</a></h2>

<p>Fibers (or coroutines) are lightweight application threads. Multiple fibers can be running on top of a single system thread. Unlike system threads, all the context switching between fibers is happening explicitly. Because of this every such context switch is very fast (~200 million of fiber context switches can be made per second on a single CPU core).</p>

<p>folly::fibers implements a task manager (FiberManager), which executes scheduled tasks on fibers. It also provides some fiber-compatible synchronization primitives.</p>

<h2 id="basic-example">Basic example <a href="#basic-example" class="headerLink">#</a></h2>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">...</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Baton">Baton</span> <span class="no">baton</span><span class="o">;</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task 1: start&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
  <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">();</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task 1: after baton.wait()&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
<span class="o">&#125;);</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task 2: start&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
  <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="post">post</span><span class="o">();</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task 2: after baton.post()&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loop">loop</span><span class="o">();</span>
<span class="o">...</span></pre></div>

<p>This would print:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">Task</span> <span class="mi">1</span><span class="o">:</span> <span class="no">start</span>
<span class="no">Task</span> <span class="mi">2</span><span class="o">:</span> <span class="no">start</span>
<span class="no">Task</span> <span class="mi">2</span><span class="o">:</span> <span class="no">after</span> <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="post">post</span><span class="o">()</span>
<span class="no">Task</span> <span class="mi">1</span><span class="o">:</span> <span class="no">after</span> <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">()</span></pre></div>

<p>It&#039;s very important to note that both tasks in this example were executed on the same system thread. Task 1 was suspended by <tt>baton.wait()</tt> call. Task 2 then started and called <tt>baton.post()</tt>, resuming Task 1.</p>

<h2 id="features">Features <a href="#features" class="headerLink">#</a></h2>

<ul>
<li>Fibers creation and scheduling is performed by FiberManager</li>
<li>Integration with any event-management system (e.g. EventBase)</li>
<li>Low-level synchronization primitives (Baton) as well as higher-level primitives built on top of them (await, collectN, mutexes, ... )</li>
<li>Synchronization primitives have timeout support</li>
<li>Built-in mechanisms for fiber stack-overflow detection</li>
<li>Optional fiber-local data (i.e. equivalent of thread locals)</li>
</ul>

<h2 id="non-features">Non-features <a href="#non-features" class="headerLink">#</a></h2>

<ul>
<li>Individual fibers scheduling can&#039;t be directly controlled by application</li>
<li>FiberManager is not thread-safe (we recommend to keep one FiberManager per thread). Application is responsible for managing it&#039;s own threads and distributing load between them</li>
<li>We don&#039;t support automatic stack size adjustments. Each fiber has a stack of fixed size.</li>
</ul>

<h2 id="why-would-i-not-want-to">Why would I not want to use fibers ? <a href="#why-would-i-not-want-to" class="headerLink">#</a></h2>

<p>The only real downside to using fibers is the need to keep a pre-allocated stack for every fiber being run. That either makes you application use a lot of memory (if you have many concurrent tasks and each of them uses large stacks) or creates a risk of stack overflow bugs (if you try to reduce the stack size).</p>

<p>We believe these problems can be addressed (and we provide some tooling for that), as fibers library is used in many critical applications at Facebook (mcrouter, TAO, Service Router). However, it&#039;s important to be aware of the risks and be ready to deal with stack issues if you decide to use fibers library in your application.</p>

<h2 id="what-are-the-alternative">What are the alternatives ? <a href="#what-are-the-alternative" class="headerLink">#</a></h2>

<ul>
<li><a href="https://github.com/facebook/folly/blob/master/folly/futures/" target="_blank">Futures</a> library works great for asynchronous high-level application code. Yet code written using fibers library is generally much simpler and much more efficient (you are not paying the penalty of heap allocation).</li>
<li>You can always keep writing your asynchronous code using traditional callback approach. Such code quickly becomes hard to manage and is error-prone. Even though callback approach seems to allow you writing the most efficient code, inefficiency still comes from heap allocations (<tt>std::function</tt>s used for callbacks, context objects to be passed between callbacks etc.)</li>
</ul></section><section class="dex_document"><h1>Quick guide</h1><p class="dex_introduction"></p><p>Let&#039;s take a look at this basic example:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">...</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Baton">Baton</span> <span class="no">baton</span><span class="o">;</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task: start&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
  <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">();</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Task: after baton.wait()&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loop">loop</span><span class="o">();</span>

<span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="post">post</span><span class="o">();</span>
<span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="cout">cout</span> <span class="o">&lt;&lt;</span> <span class="s2">&quot;Baton posted&quot;</span> <span class="o">&lt;&lt;</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="endl">endl</span><span class="o">;</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loop">loop</span><span class="o">();</span>

<span class="o">...</span></pre></div>

<p>This would print:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">Task</span><span class="o">:</span> <span class="no">start</span>
<span class="no">Baton</span> <span class="no">posted</span>
<span class="no">Task</span><span class="o">:</span> <span class="no">after</span> <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">()</span></pre></div>

<p>What makes fiber-task different from any other task run on e.g. an <tt>folly::EventBase</tt> is the ability to suspend such task, without blocking the system thread. So how do you suspend a fiber-task ?</p>

<h3 id="fibers-baton">fibers::Baton <a href="#fibers-baton" class="headerLink">#</a></h3>

<p><tt>fibers::Baton</tt> is the core synchronization primitive which is used to suspend a fiber-task and notify when the task may be resumed. <tt>fibers::Baton</tt> supports two basic operations: <tt>wait()</tt> and <tt>post()</tt>. Calling <tt>wait()</tt> on a Baton will suspend current fiber-task until <tt>post()</tt> is called on the same Baton.</p>

<p>Please refer to <a href="https://github.com/facebook/folly/blob/master/folly/fibers/Baton.h" target="_blank">Baton</a> for more detailed documentation.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> <tt>fibers::Baton</tt> is the only native synchronization primitive of folly::fibers library. All other synchronization primitives provided by folly::fibers are built on top of <tt>fibers::Baton</tt>.</div>

<h3 id="integrating-with-other-a">Integrating with other asynchronous APIs (callbacks) <a href="#integrating-with-other-a" class="headerLink">#</a></h3>

<p>Let&#039;s say we have some existing library which provides a classic callback-style asynchronous API.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">void</span> <span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">Request</span> <span class="no">request</span><span class="o">,</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="Function">Function</span><span class="o">&lt;</span><span class="nf" data-symbol-name="void">void</span><span class="o">(</span><span class="no">Response</span><span class="o">)&gt;</span> <span class="no">cb</span><span class="o">);</span></pre></div>

<p>If we use folly::fibers we can just make an async call from a fiber-task and wait until callback is run:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">Response</span> <span class="no">response</span><span class="o">;</span>
  <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-context="fibers" data-symbol-name="Baton">Baton</span> <span class="no">baton</span><span class="o">;</span>

  <span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="o">[&amp;](</span><span class="no">Response</span> <span class="no">r</span><span class="o">)</span> <span class="no">mutable</span> <span class="o">&#123;</span>
     <span class="no">response</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">r</span><span class="o">);</span>
     <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="post">post</span><span class="o">();</span>
  <span class="o">&#125;);</span>
  <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">();</span>

  <span class="c">// Now response holds response returned by the async call</span>
  <span class="o">...</span>
<span class="o">&#125;</span></pre></div>

<p>Using <tt>fibers::Baton</tt> directly is generally error-prone. To make the task above simpler, folly::fibers provide <tt>fibers::await</tt> function.</p>

<p>With <tt>fibers::await</tt>, the code above transforms into:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="nf" data-symbol-context="fibers" data-symbol-name="await">await</span><span class="o">([&amp;](</span><span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-context="fibers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">Response</span><span class="o">&gt;</span> <span class="no">promise</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="o">[</span><span class="no">promise</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">promise</span><span class="o">)](</span><span class="no">Response</span> <span class="no">r</span><span class="o">)</span> <span class="no">mutable</span> <span class="o">&#123;</span>
      <span class="no">promise</span><span class="o">.</span><span class="nf" data-symbol-name="setValue">setValue</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">r</span><span class="o">));</span>
    <span class="o">&#125;);</span>
  <span class="o">&#125;);</span>

  <span class="c">// Now response holds response returned by the async call</span>
  <span class="o">...</span>
<span class="o">&#125;</span></pre></div>

<p>Callback passed to <tt>fibers::await</tt> is executed immediately and then fiber-task is suspended until <tt>fibers::Promise</tt> is fulfilled. When <tt>fibers::Promise</tt> is fulfilled with a value or exception, fiber-task will be resumed and &#039;fibers::await&#039; returns that value (or throws an exception, if exception was used to fulfill the <tt>Promise</tt>).</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="k">try</span> <span class="o">&#123;</span>
    <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="nf" data-symbol-context="fibers" data-symbol-name="await">await</span><span class="o">([&amp;](</span><span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-context="fibers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">Response</span><span class="o">&gt;</span> <span class="no">promise</span><span class="o">)</span> <span class="o">&#123;</span>
      <span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="o">[</span><span class="no">promise</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">promise</span><span class="o">)](</span><span class="no">Response</span> <span class="no">r</span><span class="o">)</span> <span class="no">mutable</span> <span class="o">&#123;</span>
        <span class="no">promise</span><span class="o">.</span><span class="nf" data-symbol-name="setException">setException</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="runtime_error">runtime_error</span><span class="o">(</span><span class="s2">&quot;Await will re-throw me&quot;</span><span class="o">));</span>
      <span class="o">&#125;);</span>
    <span class="o">&#125;);</span>
    <span class="nf" data-symbol-name="assert">assert</span><span class="o">(</span><span class="kc">false</span><span class="o">);</span> <span class="c">// We should never get here</span>
  <span class="o">&#125;</span> <span class="k">catch</span> <span class="o">(</span><span class="nc" data-symbol-name="const">const</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-name="exception">exception</span><span class="o">&amp;</span> <span class="no">e</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="nf" data-symbol-name="assert">assert</span><span class="o">(</span><span class="no">e</span><span class="o">.</span><span class="nf" data-symbol-name="what">what</span><span class="o">()</span> <span class="o">==</span> <span class="s2">&quot;Await will re-throw me&quot;</span><span class="o">);</span>
  <span class="o">&#125;</span>
  <span class="o">...</span>
<span class="o">&#125;</span></pre></div>

<p>If <tt>fibers::Promise</tt> is not fulfilled, <tt>fibers::await</tt> will throw a <tt>std::logic_error</tt>.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="k">try</span> <span class="o">&#123;</span>
    <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="nf" data-symbol-context="fibers" data-symbol-name="await">await</span><span class="o">([&amp;](</span><span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-context="fibers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">Response</span><span class="o">&gt;</span> <span class="no">promise</span><span class="o">)</span> <span class="o">&#123;</span>
      <span class="c">// We forget about the promise</span>
    <span class="o">&#125;);</span>
    <span class="nf" data-symbol-name="assert">assert</span><span class="o">(</span><span class="kc">false</span><span class="o">);</span> <span class="c">// We should never get here</span>
  <span class="o">&#125;</span> <span class="k">catch</span> <span class="o">(</span><span class="nc" data-symbol-name="const">const</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-name="logic_error">logic_error</span><span class="o">&amp;</span> <span class="no">e</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="o">...</span>
  <span class="o">&#125;</span>
  <span class="o">...</span>
<span class="o">&#125;</span></pre></div>

<p>Please refer to <a href="https://github.com/facebook/folly/blob/master/folly/fibers/Promise.h" target="_blank">await</a> for more detailed documentation.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> most of your code written with folly::fibers, won&#039;t be using <tt>fibers::Baton</tt> or <tt>fibers::await</tt>. These primitives should only be used to integrate with other asynchronous API which are not fibers-compatible.</div>

<h3 id="integrating-with-other-a-1">Integrating with other asynchronous APIs (folly::Future) <a href="#integrating-with-other-a-1" class="headerLink">#</a></h3>

<p>Let&#039;s say we have some existing library which provides a Future-based asynchronous API.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="Future">Future</span><span class="o">&lt;</span><span class="no">Response</span><span class="o">&gt;</span> <span class="nf" data-symbol-name="asyncCallFuture">asyncCallFuture</span><span class="o">(</span><span class="no">Request</span> <span class="no">request</span><span class="o">);</span></pre></div>

<p>The good news are, <tt>folly::Future</tt> is already fibers-compatible. You can simply write:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nf" data-symbol-name="asyncCallFuture">asyncCallFuture</span><span class="o">(</span><span class="no">request</span><span class="o">).</span><span class="nf" data-symbol-name="get">get</span><span class="o">();</span>

  <span class="c">// Now response holds response returned by the async call</span>
  <span class="o">...</span>
<span class="o">&#125;</span></pre></div>

<p>Calling <tt>get()</tt> on a <tt>folly::Future</tt> object will only suspend the calling fiber-task. It won&#039;t block the system thread, letting it process other tasks.</p>

<h2 id="writing-code-with-folly">Writing code with folly::fibers <a href="#writing-code-with-folly" class="headerLink">#</a></h2>

<h3 id="building-fibers-compatib">Building fibers-compatible API <a href="#building-fibers-compatib" class="headerLink">#</a></h3>

<p>Following the explanations above we may wrap an existing asynchronous API in a function:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">Response</span> <span class="nf" data-symbol-name="fiberCall">fiberCall</span><span class="o">(</span><span class="no">Request</span> <span class="no">request</span><span class="o">)</span> <span class="o">&#123;</span>
  <span class="k">return</span> <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="nf" data-symbol-context="fibers" data-symbol-name="await">await</span><span class="o">([&amp;](</span><span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-context="fibers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">Response</span><span class="o">&gt;</span> <span class="no">promise</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="o">[</span><span class="no">promise</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">promise</span><span class="o">)](</span><span class="no">Response</span> <span class="no">r</span><span class="o">)</span> <span class="no">mutable</span> <span class="o">&#123;</span>
      <span class="no">promise</span><span class="o">.</span><span class="nf" data-symbol-name="setValue">setValue</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">r</span><span class="o">));</span>
    <span class="o">&#125;);</span>
  <span class="o">&#125;);</span>
<span class="o">&#125;</span></pre></div>

<p>We can then call it from a fiber-task:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nf" data-symbol-name="fiberCall">fiberCall</span><span class="o">(</span><span class="no">request</span><span class="o">);</span>
  <span class="o">...</span>
<span class="o">&#125;);</span></pre></div>

<p>But what happens if we just call <tt>fiberCall</tt> not from within a fiber-task, but directly from a system thread ? Here another important feature of <tt>fibers::Baton</tt> (and thus all other folly::fibers synchronization primitives built on top of it) comes into play. Calling <tt>wait()</tt> on a <tt>fibers::Baton</tt> within a system thread context just blocks the thread until <tt>post()</tt> is called on the same <tt>folly::Baton</tt>.</p>

<p>What this means is that you no longer need to write separate code for synchronous and asynchronous APIs. If you use only folly::fibers synchronization primitives for all blocking calls inside of your synchronous function, it automatically becomes asynchronous when run inside a fiber-task.</p>

<h3 id="passing-by-reference">Passing by reference <a href="#passing-by-reference" class="headerLink">#</a></h3>

<p>Classic asynchronous APIs (same applies to folly::Future-based APIs) generally rely on copying/moving-in input arguments and often require you to copy/move in some context variables into the callback. E.g.:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">...</span>
<span class="no">Context</span> <span class="no">context</span><span class="o">;</span>

<span class="nf" data-symbol-name="asyncCall">asyncCall</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="o">[</span><span class="no">request</span><span class="o">,</span> <span class="no">context</span><span class="o">](</span><span class="no">Response</span> <span class="no">response</span><span class="o">)</span> <span class="no">mutable</span> <span class="o">&#123;</span>
  <span class="nf" data-symbol-name="doSomething">doSomething</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="no">response</span><span class="o">,</span> <span class="no">context</span><span class="o">);</span>
<span class="o">&#125;);</span>
<span class="o">...</span></pre></div>

<p>Fibers-compatible APIs look more like synchronous APIs, so you can actually pass input arguments by reference and you don&#039;t have to think about passing context at all. E.g.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">Context</span> <span class="no">context</span><span class="o">;</span>

  <span class="no">auto</span> <span class="no">response</span> <span class="o">=</span> <span class="nf" data-symbol-name="fiberCall">fiberCall</span><span class="o">(</span><span class="no">request</span><span class="o">);</span>

  <span class="nf" data-symbol-name="doSomething">doSomething</span><span class="o">(</span><span class="no">request</span><span class="o">,</span> <span class="no">response</span><span class="o">,</span> <span class="no">context</span><span class="o">);</span>
  <span class="o">...</span>
<span class="o">&#125;);</span></pre></div>

<p>Same logic applies to <tt>fibers::await</tt>. Since <tt>fibers::await</tt> call blocks until promise is fulfilled, it&#039;s safe to pass everything by reference.</p>

<h3 id="limited-stack-space">Limited stack space <a href="#limited-stack-space" class="headerLink">#</a></h3>

<p>So should you just run all the code inside a fiber-task ? No exactly.</p>

<p>Similarly to system threads, every fiber-task has some stack space assigned to it. Stack usage goes up with the number of nested function calls and objects allocated on the stack. folly::fibers implementation only supports fiber-tasks with fixed stack size. If you want to have many fiber-tasks running concurrently - you need to reduce the amount of stack assigned to each fiber-task, otherwise you may run out of memory.</p>

<div class="remarkup-important"><span class="remarkup-note-word">IMPORTANT:</span> If a fiber-task runs out of stack space (e.g. calls a function which does a lot of stack allocations) you program will fail.</div>

<p>However if you know that some function never suspends a fiber-task, you can use <tt>fibers::runInMainContext</tt> to safely call it from a fiber-task, without any risk of running out of stack space of the fiber-task.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">Result</span> <span class="nf" data-symbol-name="useALotOfStack">useALotOfStack</span><span class="o">()</span> <span class="o">&#123;</span>
  <span class="no">char</span> <span class="no">buffer</span><span class="o">[</span><span class="mi">1024</span><span class="o">*</span><span class="mi">1024</span><span class="o">];</span>
  <span class="o">...</span>
<span class="o">&#125;</span>

<span class="o">...</span>
<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="no">auto</span> <span class="no">result</span> <span class="o">=</span> <span class="nc" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="nf" data-symbol-context="fibers" data-symbol-name="runInMainContext">runInMainContext</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
    <span class="k">return</span> <span class="nf" data-symbol-name="useALotOfStack">useALotOfStack</span><span class="o">();</span>
  <span class="o">&#125;);</span>
  <span class="o">...</span>
<span class="o">&#125;);</span>
<span class="o">...</span></pre></div>

<p><tt>fibers::runInMainContext</tt> will switch to the stack of the system thread (main context), run the functor passed to it and then switch back to the fiber-task stack.</p>

<div class="remarkup-important"><span class="remarkup-note-word">IMPORTANT:</span> Make sure you don&#039;t do any blocking calls on main context though. It will suspend the whole system thread, not just the fiber-task which was running.</div>

<p>Remember that it&#039;s fine to use <tt>fibers::runInMainContext</tt> in general purpose functions (those which may be called both from fiber-task and non from fiber-task). When called in non-fiber-task context <tt>fibers::runInMainContext</tt> would simply execute passed functor right away.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> Besides <tt>fibers::runInMainContext</tt> some other functions in folly::fibers are also executing some of the passed functors on the main context. E.g. functor passes to <tt>fibers::await</tt> is executed on main context, finally-functor passed to <tt>FiberManager::addTaskFinally</tt> is also executed on main context etc. Relying on this can help you avoid extra <tt>fibers::runInMainContext</tt> calls (and avoid extra context switches).</div>

<h3 id="using-locks">Using locks <a href="#using-locks" class="headerLink">#</a></h3>

<p>Consider the following example:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">...</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>
<span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="mutex">mutex</span> <span class="no">lock</span><span class="o">;</span>
<span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Baton">Baton</span> <span class="no">baton</span><span class="o">;</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="lock_guard">lock_guard</span><span class="o">&lt;</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="mutex">mutex</span><span class="o">&gt;</span> <span class="nf" data-symbol-name="lg">lg</span><span class="o">(</span><span class="no">lock</span><span class="o">);</span>
  <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="wait">wait</span><span class="o">();</span>
<span class="o">&#125;);</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="lock_guard">lock_guard</span><span class="o">&lt;</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="mutex">mutex</span><span class="o">&gt;</span> <span class="nf" data-symbol-name="lg">lg</span><span class="o">(</span><span class="no">lock</span><span class="o">);</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loop">loop</span><span class="o">();</span>
<span class="c">// We won&#039;t get here :(</span>
<span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="post">post</span><span class="o">();</span>
<span class="o">...</span></pre></div>

<p>First fiber-task will grab a lock and then suspend waiting on a <tt>fibers::Baton</tt>. Then second fiber-task will be run and it will try to grab a lock. Unlike system threads, fiber-task can be only suspended explicitly, so the whole system thread will be blocked waiting on the lock, and we end up with a dead-lock.</p>

<p>There&#039;re generally two ways we can solve this problem. Ideally we would re-design the program to never not hold any locks when fiber-task is suspended. However if we are absolutely sure we need that lock - folly::fibers library provides some fiber-task-aware lock implementations (e.g.
<a href="https://github.com/facebook/folly/blob/master/folly/fibers/TimedMutex.h" target="_blank">TimedMutex</a>).</p></section><section class="dex_document"><h1>APIs</h1><p class="dex_introduction"></p><h2 id="fibers-baton">fibers::Baton <a href="#fibers-baton" class="headerLink">#</a></h2>

<p>All of the features of folly::fibers library are actually built on top a single synchronization primitive called Baton. <tt>fibers::Baton</tt> is a fiber-specific version of <tt>folly::Baton</tt>. It only  supports two basic operations: <tt>wait()</tt> and <tt>post()</tt>. Whenever <tt>wait()</tt> is called on the Baton, the current thread or fiber-task is suspended, until <tt>post()</tt> is called on the same Baton. <tt>wait()</tt> does not suspend the thread or fiber-task if <tt>post()</tt> was already called on the Baton. Please refer to <a href="https://github.com/facebook/folly/blob/master/folly/fibers/Baton.h" target="_blank">Baton</a> for more detailed documentation.</p>

<p>Baton is thread-safe, so <tt>wait()</tt> and <tt>post()</tt> can be (and should be :) ) called from different threads or fiber-tasks.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> Because Baton transparently works both on threads and fiber-tasks, any synchronization primitive built using it would have the same property. This means that any library with a synchronous API, which uses only <tt>fibers::Baton</tt> for synchronization, becomes asynchronous when used in fiber context.</div>

<h3 id="timed-wait">timed_wait() <a href="#timed-wait" class="headerLink">#</a></h3>

<p><tt>fibers::Baton</tt> also supports wait with timeout.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([=]()</span> <span class="o">&#123;</span>
  <span class="no">auto</span> <span class="no">baton</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="make_shared">make_shared</span><span class="o">&lt;</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Baton">Baton</span><span class="o">&gt;();</span>
  <span class="no">auto</span> <span class="no">result</span> <span class="o">=</span> <span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="make_shared">make_shared</span><span class="o">&lt;</span><span class="no">Result</span><span class="o">&gt;();</span>

  <span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([=]()</span> <span class="o">&#123;</span>
    <span class="o">*</span><span class="no">result</span> <span class="o">=</span> <span class="nf" data-symbol-name="sendRequest">sendRequest</span><span class="o">(...);</span>
    <span class="no">baton</span><span class="o">-&gt;</span><span class="na" data-symbol-name="post">post</span><span class="o">();</span>
  <span class="o">&#125;);</span>

  <span class="no">bool</span> <span class="no">success</span> <span class="o">=</span> <span class="no">baton</span><span class="o">.</span><span class="nf" data-symbol-name="timed_wait">timed_wait</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="chrono">chrono</span><span class="o">::</span><span class="na" data-symbol-name="milliseconds">milliseconds</span><span class="o">&#123;</span><span class="mi">10</span><span class="o">&#125;);</span>
  <span class="k">if</span> <span class="o">(</span><span class="no">success</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="c">// request successful</span>
    <span class="o">...</span>
  <span class="o">&#125;</span> <span class="k">else</span> <span class="o">&#123;</span>
    <span class="c">// handle timeout</span>
    <span class="o">...</span>
  <span class="o">&#125;</span>
<span class="o">&#125;);</span></pre></div>

<div class="remarkup-important"><span class="remarkup-note-word">IMPORTANT:</span> unlike <tt>wait()</tt> when using <tt>timed_wait()</tt> API it&#039;s generally not safe to pass <tt>fibers::Baton</tt> by reference. You have to make sure that task, which fulfills the Baton is either cancelled in case of timeout, or have shared ownership for the Baton.</div>

<h2 id="task-creation">Task creation <a href="#task-creation" class="headerLink">#</a></h2>

<h3 id="addtask-addtaskremote">addTask() / addTaskRemote() <a href="#addtask-addtaskremote" class="headerLink">#</a></h3>

<p>As you could see from previous examples, the easiest way to create a new fiber-task is to call <tt>addTask()</tt>:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
<span class="o">&#125;);</span></pre></div>

<p>It is important to remember that <tt>addTask()</tt> is not thread-safe. I.e. it can only be safely called from the the thread, which is running the <tt>folly::FiberManager</tt> loop.</p>

<p>If you need to create a fiber-task from a different thread, you have to use <tt>addTaskRemote()</tt>:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>

<span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="thread">thread</span> <span class="nf" data-symbol-name="t">t</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTaskRemote">addTaskRemote</span><span class="o">([]()</span> <span class="o">&#123;</span>
    <span class="o">...</span>
  <span class="o">&#125;);</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loopForever">loopForever</span><span class="o">();</span></pre></div>

<h3 id="addtaskfinally">addTaskFinally() <a href="#addtaskfinally" class="headerLink">#</a></h3>

<p><tt>addTaskFinally()</tt> is useful when you need to run some code on the main context in the end of a fiber-task.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTaskFinally">addTaskFinally</span><span class="o">(</span>
  <span class="o">[=]()</span> <span class="o">&#123;</span>
    <span class="o">...</span>
    <span class="k">return</span> <span class="no">result</span><span class="o">;</span>
  <span class="o">&#125;,</span>
  <span class="o">[=](</span><span class="no">Result</span><span class="o">&amp;&amp;</span> <span class="no">result</span><span class="o">)</span> <span class="o">&#123;</span>
    <span class="nf" data-symbol-name="callUserCallbacks">callUserCallbacks</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">result</span><span class="o">),</span> <span class="o">...)</span>
  <span class="o">&#125;</span>
<span class="o">);</span></pre></div>

<p>Of course you could achieve the same by calling <tt>fibers::runInMainContext()</tt>, but <tt>addTaskFinally()</tt> reduces the number of fiber context switches:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([=]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
  <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="runInMainContext">runInMainContext</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
    <span class="c">// Switched to main context</span>
    <span class="nf" data-symbol-name="callUserCallbacks">callUserCallbacks</span><span class="o">(</span><span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="nf" data-symbol-context="std" data-symbol-name="move">move</span><span class="o">(</span><span class="no">result</span><span class="o">),</span> <span class="o">...)</span>
  <span class="o">&#125;</span>
  <span class="c">// Switched back to fiber context</span>

  <span class="c">// On fiber context we realize there&#039;s no more work to be done.</span>
  <span class="c">// Fiber-task is complete, switching back to main context.</span>
<span class="o">&#125;);</span></pre></div>

<p></p>

<h3 id="addtaskfuture-addtaskrem">addTaskFuture() / addTaskRemoteFuture() <a href="#addtaskfuture-addtaskrem" class="headerLink">#</a></h3>

<p><tt>addTask()</tt> and <tt>addTaskRemote()</tt> are creating detached fiber-tasks. If you need to know when fiber-task is complete and/or have some return value for it -  <tt>addTaskFuture()</tt> / <tt>addTaskRemoteFuture()</tt> can be used.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>

<span class="nc" data-symbol-name="std">std</span><span class="o">::</span><span class="na" data-symbol-context="std" data-symbol-name="thread">thread</span> <span class="nf" data-symbol-name="t">t</span><span class="o">([&amp;]()</span> <span class="o">&#123;</span>
  <span class="no">auto</span> <span class="no">future1</span> <span class="o">=</span> <span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTaskRemoteFuture">addTaskRemoteFuture</span><span class="o">([]()</span> <span class="o">&#123;</span>
    <span class="o">...</span>
  <span class="o">&#125;);</span>
  <span class="no">auto</span> <span class="no">future2</span> <span class="o">=</span> <span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTaskRemoteFuture">addTaskRemoteFuture</span><span class="o">([]()</span> <span class="o">&#123;</span>
    <span class="o">...</span>
  <span class="o">&#125;);</span>

  <span class="no">auto</span> <span class="no">result1</span> <span class="o">=</span> <span class="no">future1</span><span class="o">.</span><span class="nf" data-symbol-name="get">get</span><span class="o">();</span>
  <span class="no">auto</span> <span class="no">result2</span> <span class="o">=</span> <span class="no">future2</span><span class="o">.</span><span class="nf" data-symbol-name="get">get</span><span class="o">();</span>
  <span class="o">...</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loopForever">loopForever</span><span class="o">();</span></pre></div>

<h2 id="other-synchronization-pr">Other synchronization primitives <a href="#other-synchronization-pr" class="headerLink">#</a></h2>

<p>All the listed synchronization primitives are built using <tt>fiber::Baton</tt>. Please check their source code for detailed documentation.</p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/Promise.h" target="_blank">await</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/WhenN.h" target="_blank">collectN</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/WhenN.h" target="_blank">collectAny</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/WhenN.h" target="_blank">collectN</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/ForEach.h" target="_blank">forEach</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/AddTasks.h" target="_blank">addTasks</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/TimedMutex.h" target="_blank">TimedMutex</a></p>

<p><a href="https://github.com/facebook/folly/blob/master/folly/fibers/TimedMutex.h" target="_blank">TimedRWMutex</a></p></section><section class="dex_document"><h1>Fiber stacks</h1><p class="dex_introduction"></p><p>Similarly to system threads, every fiber-task has some stack space assigned to it. Stack usage goes up with the number of nested function calls and objects allocated on the stack. folly::fibers implementation only supports fiber-tasks with fixed stack size. If you want to have many fiber-tasks running concurrently - you need to reduce the amount of stack assigned to each fiber-task, otherwise you may run out of memory.</p>

<h3 id="selecting-stack-size">Selecting stack size <a href="#selecting-stack-size" class="headerLink">#</a></h3>

<p>Stack size used for every fiber-task is part of FiberManager configuration. But how do you pick the right stack size ?</p>

<p>First of all you need to figure out the maximum number of concurrent fiber-tasks your application may have. E.g. if you are writing a Thrift-service you will probably have a single fiber-task for every request in-fly (but remember that e.g. <tt>fibers::collectAll</tt> and some other synchronization primitives may create extra fiber-tasks). It&#039;s very important to get that number first, because if you will at most need 100 concurrent fiber-tasks, even 1MB stacks will result in at most 100MB used for fiber stacks. On the other hand if you need to have 100,000 concurrent fiber-tasks, even 16KB stacks will result in 1.6GB peak memory usage just for fiber stacks.</p>

<p>folly::fibers also supports recording stack usage (it can be enabled via <tt>recordStackEvery</tt> option of <tt>FiberManager</tt>). When enabled, the stack of each fiber-task will be filled with magic values. Later linear search can be performed to find the boundary of unused stack space.</p>

<h3 id="stack-overflow-detection">Stack overflow detection <a href="#stack-overflow-detection" class="headerLink">#</a></h3>

<p>By default every fiber-task stack is allocated with a special guard page next to it (this can be controlled via <tt>useGuardPages</tt> option of <tt>FiberManager</tt>). If a stack overflow happens - this guard page will be accessed, which will result in immediate segmentation fault.</p>

<div class="remarkup-important"><span class="remarkup-note-word">IMPORTANT:</span> disabling guard page protection may result in unnoticed stack overflows. Those will inevitably cause memory corruptions, which are usually very hard to debug.</div></section><section class="dex_document"><h1>Event Loops</h1><p class="dex_introduction"></p><p>folly::fibers library doesn&#039;t implement it&#039;s own event system. Instead it allows <tt>fibers::FiberManager</tt> to work with any other event system by implementing <tt>fibers::LoopController</tt> interface.</p>

<h2 id="folly-eventbase-integrat">folly::EventBase integration <a href="#folly-eventbase-integrat" class="headerLink">#</a></h2>

<p>The easiest way to create a <tt>fibers::FiberManager</tt> attached to a <tt>folly::EventBase</tt> is by using <tt>fibers::getFiberManager</tt> function:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span> <span class="no">evb</span><span class="o">;</span>
<span class="no">auto</span><span class="o">&amp;</span> <span class="no">fiberManager</span> <span class="o">=</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="getFiberManager">getFiberManager</span><span class="o">(</span><span class="no">evb</span><span class="o">);</span>

<span class="no">fiberManager</span><span class="o">.</span><span class="nf" data-symbol-name="addTask">addTask</span><span class="o">([]()</span> <span class="o">&#123;</span>
  <span class="o">...</span>
<span class="o">&#125;);</span>

<span class="no">evb</span><span class="o">.</span><span class="nf" data-symbol-name="loop">loop</span><span class="o">();</span></pre></div>

<p>Such <tt>fibers::FiberManager</tt> will be automatically destroyed, when <tt>folly::EventBase</tt> is destroyed.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> folly::fibers doesn&#039;t support killing fiber-tasks in-flight (for similar reasons you can&#039;t kill a thread). If <tt>fibers::FiberManager</tt> has any outstanding fiber-tasks, when <tt>folly::EventBase</tt> is being destroyed, it will keep running the event loop until all those tasks are finished.</div></section><section class="dex_document"><h1>GDB integration</h1><p class="dex_introduction"></p><p>folly::fibers provide some GDB extensions which can be very useful for debugging. To load them simply the following in dbg console:</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="no">source</span> <span class="s1">&#039;folly/fibers/scripts/utils.gdb&#039;</span></pre></div>

<h3 id="show-all-fibermanagers">Show all FiberManagers <a href="#show-all-fibermanagers" class="headerLink">#</a></h3>

<p>You can use <tt>print_folly_fiber_manager_map</tt> to list all <tt>fibers::FiberManager</tt>s and <tt>folly::EventBase</tt>s they are attached to.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">(</span><span class="no">gdb</span><span class="o">)</span> <span class="no">print_folly_fiber_manager_map</span>
  <span class="k">Global</span> <span class="no">FiberManager</span> <span class="no">map</span> <span class="no">has</span> <span class="mi">2</span> <span class="no">entries</span><span class="o">.</span>
    <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span><span class="o">*)</span><span class="mh">0x7fffffffdb60</span> <span class="o">-&gt;</span> <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="FiberManager">FiberManager</span><span class="o">*)</span><span class="mh">0x7ffff5b58480</span>
    <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="EventBase">EventBase</span><span class="o">*)</span><span class="mh">0x7fffffffd930</span> <span class="o">-&gt;</span> <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="FiberManager">FiberManager</span><span class="o">*)</span><span class="mh">0x7ffff5b58300</span></pre></div>

<p>This will only list <tt>fibers::FiberManager</tt>s created using <tt>fibers::getFiberManager()</tt> function.</p>

<h3 id="print-fibermanager-state">Print FiberManager state <a href="#print-fibermanager-state" class="headerLink">#</a></h3>

<p>You can use <tt>print_folly_fiber_manager</tt> (and passing a pointer to valid <tt>fibers::FiberManager</tt> object) to print the state of given <tt>fibers::FiberManager</tt>.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">(</span><span class="no">gdb</span><span class="o">)</span> <span class="no">print_folly_fiber_manager</span> <span class="o">&amp;</span><span class="nf" data-symbol-name="manager">manager</span>
  <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="FiberManager">FiberManager</span><span class="o">*)</span><span class="mh">0x7fffffffdbe0</span>

  <span class="no">Fibers</span> <span class="no">active</span><span class="o">:</span> <span class="mi">3</span>
  <span class="no">Fibers</span> <span class="no">allocated</span><span class="o">:</span> <span class="mi">3</span>
  <span class="no">Fibers</span> <span class="no">pool</span> <span class="no">size</span><span class="o">:</span> <span class="mi">0</span>
  <span class="no">Active</span> <span class="no">fiber</span><span class="o">:</span> <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)(</span><span class="no">nil</span><span class="o">)</span>
  <span class="no">Current</span> <span class="no">fiber</span><span class="o">:</span> <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)(</span><span class="no">nil</span><span class="o">)</span>

  <span class="no">Active</span> <span class="no">fibers</span><span class="o">:</span>
    <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)</span><span class="mh">0x7ffff5b5b000</span>   <span class="no">State</span><span class="o">:</span> <span class="nf" data-symbol-name="Awaiting">Awaiting</span>
    <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)</span><span class="mh">0x7ffff5b5b300</span>   <span class="no">State</span><span class="o">:</span> <span class="nf" data-symbol-name="Awaiting">Awaiting</span>
    <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)</span><span class="mh">0x7ffff5b5b600</span>   <span class="no">State</span><span class="o">:</span> <span class="no">Awaiting</span></pre></div>

<p>It will list all active <tt>fibers::Fiber</tt> objects, which are running fiber-tasks and their states.</p>

<h3 id="print-fiber-task">Print fiber-task <a href="#print-fiber-task" class="headerLink">#</a></h3>

<p>If you have a <tt>fibers::Fiber</tt>, which is running some fiber-task, you can print its state using <tt>print_folly_fiber</tt> command.</p>

<div class="remarkup-code-block" data-code-lang="php"><pre class="remarkup-code"><span class="o">(</span><span class="no">gdb</span><span class="o">)</span> <span class="no">print_folly_fiber</span> <span class="mh">0x7ffff5b5b600</span>
  <span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Fiber">Fiber</span><span class="o">*)</span><span class="mh">0x7ffff5b5b600</span>

  <span class="no">State</span><span class="o">:</span> <span class="no">Awaiting</span>
  <span class="no">Backtrace</span><span class="o">:</span>
    <span class="c">#0 at 0x5a22a8 in folly::fibers::FiberManager::deactivateFiber(folly::fibers::Fiber*) + 194 in section .text of /mnt/fio0/andrii/fbsource/fbcode/buck-out</span>
<span class="o">/</span><span class="no">gen</span><span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#1 at 0x5a1606 in folly::fibers::Fiber::preempt(folly::fibers::Fiber::State)::&#123;lambda()#1&#125;::operator()() + 598 in section .text of /mnt/fio0/andrii/fbsou</span>
<span class="no">rce</span><span class="o">/</span><span class="no">fbcode</span><span class="o">/</span><span class="no">buck</span><span class="o">-</span><span class="no">out</span><span class="o">/</span><span class="no">gen</span><span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#2 at 0x5a17f8 in folly::fibers::Fiber::preempt(folly::fibers::Fiber::State) + 176 in section .text of /mnt/fio0/andrii/fbsource/fbcode/buck-out/gen/foll</span>
<span class="no">y</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#3 at 0x43a76e in void folly::fibers::Baton::waitFiber&lt;folly::fibers::FirstArgOf&lt;FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::operator()() con</span>
<span class="nc" data-symbol-name="st">st</span><span class="o">::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;, void&gt;::type::value_type folly::fibers::await</span>
<span class="o">&lt;</span><span class="nc" data-symbol-name="FiberManager_collectAll_Test">FiberManager_collectAll_Test</span><span class="o">::</span><span class="nf" data-symbol-context="FiberManager_collectAll_Test" data-symbol-name="TestBody">TestBody</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(foll</span>
<span class="nc" data-symbol-name="y">y</span><span class="o">::</span><span class="na" data-symbol-context="y" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">int</span><span class="o">&gt;)</span><span class="c">#1&#125;&gt;(folly::fibers::Promise&lt;int&gt;&amp;&amp;)::&#123;lambda()#1&#125;&gt;(folly::fibers::FiberManager&amp;, folly::fibers::FirstArgOf&lt;FiberManager_collectAll_Te</span>
<span class="nc" data-symbol-name="st">st</span><span class="o">::</span><span class="nf" data-symbol-context="st" data-symbol-name="TestBody">TestBody</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;</span>
<span class="o">,</span> <span class="no">void</span><span class="o">&gt;::</span><span class="nc" data-symbol-name="type">type</span><span class="o">::</span><span class="na" data-symbol-context="type" data-symbol-name="value_type">value_type</span> <span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="await">await</span><span class="o">&lt;</span><span class="nc" data-symbol-name="FiberManager_collectAll_Test">FiberManager_collectAll_Test</span><span class="o">::</span><span class="nf" data-symbol-context="FiberManager_collectAll_Test" data-symbol-name="TestBody">TestBody</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::</span>
<span class="o">&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;&gt;(folly::fibers::Promise&lt;int&gt;&amp;&amp;)::&#123;lambda()#1&#125;) + 110 in section .text of /mnt/fio0/</span>
<span class="no">andrii</span><span class="o">/</span><span class="no">fbsource</span><span class="o">/</span><span class="no">fbcode</span><span class="o">/</span><span class="no">buck</span><span class="o">-</span><span class="no">out</span><span class="o">/</span><span class="no">gen</span><span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#4 at 0x42fa89 in void folly::fibers::Baton::wait&lt;folly::fibers::FirstArgOf&lt;FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::operator()() const::&#123;</span>
<span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;, void&gt;::type::value_type folly::fibers::await&lt;Fibe</span>
<span class="nc" data-symbol-name="rManager_collectAll_Test">rManager_collectAll_Test</span><span class="o">::</span><span class="nf" data-symbol-context="rManager_collectAll_Test" data-symbol-name="TestBody">TestBody</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fi</span>
<span class="nc" data-symbol-name="bers">bers</span><span class="o">::</span><span class="na" data-symbol-context="bers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">int</span><span class="o">&gt;)</span><span class="c">#1&#125;&gt;(folly::fibers::Promise&lt;int&gt;&amp;&amp;)::&#123;lambda()#1&#125;&gt;(folly::fibers::FirstArgOf&lt;FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::opera</span>
<span class="nf" data-symbol-name="tor">tor</span><span class="o">()()</span> <span class="k">const</span><span class="o">::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;, void&gt;::type::value_type folly::fi</span>
<span class="nc" data-symbol-name="bers">bers</span><span class="o">::</span><span class="na" data-symbol-context="bers" data-symbol-name="await">await</span><span class="o">&lt;</span><span class="nc" data-symbol-name="FiberManager_collectAll_Test">FiberManager_collectAll_Test</span><span class="o">::</span><span class="nf" data-symbol-context="FiberManager_collectAll_Test" data-symbol-name="TestBody">TestBody</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;</span>
<span class="nf" data-symbol-name="lambda">lambda</span><span class="o">(</span><span class="nc" data-symbol-name="folly">folly</span><span class="o">::</span><span class="na" data-symbol-context="folly" data-symbol-name="fibers">fibers</span><span class="o">::</span><span class="na" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">int</span><span class="o">&gt;)</span><span class="c">#1&#125;&gt;(folly::fibers::Promise&lt;int&gt;&amp;&amp;)::&#123;lambda()#1&#125;) + 105 in section .text of /mnt/fio0/andrii/fbsource/fbcode/buck-out/gen</span>
<span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#5 at 0x425921 in folly::fibers::FirstArgOf&lt;FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const:</span>
<span class="o">:&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;, void&gt;::type::value_type folly::fibers::await&lt;FiberManager_collectAll_Test::TestBo</span>
<span class="nf" data-symbol-name="dy">dy</span><span class="o">()::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda(folly::fibers::Promise&lt;int&gt;)#1&#125;&gt;(folly::f</span>
<span class="nc" data-symbol-name="ibers">ibers</span><span class="o">::</span><span class="na" data-symbol-context="ibers" data-symbol-name="Promise">Promise</span><span class="o">&lt;</span><span class="no">int</span><span class="o">&gt;&amp;&amp;)</span> <span class="o">+</span> <span class="mi">80</span> <span class="no">in</span> <span class="no">section</span> <span class="o">.</span><span class="no">text</span> <span class="no">of</span> <span class="o">/</span><span class="no">mnt</span><span class="o">/</span><span class="no">fio0</span><span class="o">/</span><span class="no">andrii</span><span class="o">/</span><span class="no">fbsource</span><span class="o">/</span><span class="no">fbcode</span><span class="o">/</span><span class="no">buck</span><span class="o">-</span><span class="no">out</span><span class="o">/</span><span class="no">gen</span><span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#6 at 0x415e9a in FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()(</span>
<span class="o">)</span> <span class="k">const</span> <span class="o">+</span> <span class="mi">36</span> <span class="no">in</span> <span class="no">section</span> <span class="o">.</span><span class="no">text</span> <span class="no">of</span> <span class="o">/</span><span class="no">mnt</span><span class="o">/</span><span class="no">fio0</span><span class="o">/</span><span class="no">andrii</span><span class="o">/</span><span class="no">fbsource</span><span class="o">/</span><span class="no">fbcode</span><span class="o">/</span><span class="no">buck</span><span class="o">-</span><span class="no">out</span><span class="o">/</span><span class="no">gen</span><span class="o">/</span><span class="no">folly</span><span class="o">/</span><span class="no">experimental</span><span class="o">/</span><span class="no">fibers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="c">#7 at 0x42faf9 in std::_Function_handler&lt;int (), FiberManager_collectAll_Test::TestBody()::&#123;lambda()#1&#125;::operator()() const::&#123;lambda()#1&#125;::operator()() c</span>
<span class="nc" data-symbol-name="onst">onst</span><span class="o">::&#123;</span><span class="nf" data-symbol-name="lambda">lambda</span><span class="o">()</span><span class="c">#1&#125;&gt;::_M_invoke(std::_Any_data const&amp;) + 32 in section .text of /mnt/fio0/andrii/fbsource/fbcode/buck-out/gen/folly/fibers/fibers</span>
<span class="o">-</span><span class="no">test</span>
    <span class="c">#8 at 0x479d5c in std::function&lt;int ()&gt;::operator()() const + 50 in section .text of /mnt/fio0/andrii/fbsource/fbcode/buck-out/gen/folly/experimental/fib</span>
<span class="no">ers</span><span class="o">/</span><span class="no">fibers</span><span class="o">-</span><span class="no">test</span>
    <span class="o">...</span></pre></div>

<p>It will print the state of the fiber-task and if fiber-task is currently awaiting - also prints its stack-trace.</p>

<div class="remarkup-note"><span class="remarkup-note-word">NOTE:</span> For running (i.e. not suspended) fiber-task, you can simply switch to the system thread which owns it and use regular GDB commands for debugging.</div></section></section>