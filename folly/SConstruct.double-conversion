double_conversion_sources = ['src/' + x for x in SConscript('src/SConscript')]
double_conversion_test_sources = ['test/cctest/' + x for x in SConscript('test/cctest/SConscript')]
test = double_conversion_sources + double_conversion_test_sources
print(test)
env = Environment(CPPPATH='#/src')
debug = ARGUMENTS.get('debug', 0)
optimize = ARGUMENTS.get('optimize', 0)
if int(debug):
  env.Append(CCFLAGS = '-g -Wall -Werror')
if int(optimize):
  env.Append(CCFLAGS = '-O3')
print double_conversion_sources
print double_conversion_test_sources
double_conversion_shared_objects = [
    env.SharedObject(src) for src in double_conversion_sources]
double_conversion_static_objects = [
    env.StaticObject(src) for src in double_conversion_sources]
library_name = 'double_conversion'
static_lib = env.StaticLibrary(library_name, double_conversion_static_objects)
env.StaticLibrary(library_name + '_pic', double_conversion_shared_objects)
env.Program('run_tests', double_conversion_test_sources, LIBS=[static_lib])
