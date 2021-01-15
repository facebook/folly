fbload folly
# For folly::dynamic
fbload stl

run

#### FBString Tests ####
p empty
# CHECK: ""

p small
# CHECK: "small"

p maxsmall
# CHECK: "12345678901234567890123"

p minmed
# CHECK: "123456789012345678901234"

p large
# CHECK: "abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456abcdefghijklmnopqrstuvwxyz123456"


#### StringPiece Tests ####
p emptypiece
# CHECK: ""

p otherpiece
# CHECK: "strings. Strings! STRINGS!!"

#### Range Tests ####
p num_range
# CHECK: const int *
# CHECK: length 6
# CHECK: [0] = 1
# CHECK: [5] = 6


#### Dynamic Tests ####
p dynamic_null
# CHECK: NULL

p dynamic_array
# CHECK: length 6
# CHECK_SAME: "A string", 1, 2, 3, 4, 5

p dynamic_bool
# CHECK: true

p dynamic_double
# CHECK: .25

p dynamic_int64
# CHECK: 8675309

p dynamic_string
# CHECK: "Hi!"

p dynamic_object
# CHECK: 2 elements
# CHECK_DAG: ["one"] = "two"
# CHECK_DAG: ["eight"] = "ten"


#### IPAddress Tests ####
p ipv4
# CHECK: 0.0.0.0

p ipv6
# CHECK: 2a03:2880:fffe:000c:face:b00c:0000:0035

#### SocketAddress Tests ####
p ipv4socket
# CHECK: 127.0.0.1:8080

p ipv6socket
# CHECK: [2a03:2880:fffe:000c:face:b00c:0000:0035]:8080


#### F14 Tests ####
p m_node
# CHECK: folly::F14NodeMap with 3 elements = {["foo"] = 0, ["bar"] = 1,
# CHECK:   ["baz"] = 2}
p m_val
# CHECK: folly::F14ValueMap with 1 elements = {["foo"] = 0}
p m_vec
# CHECK: folly::F14VectorMap with 2 elements = {["foo"] = 0, ["bar"] = 1}
p m_fvec
# CHECK: folly::F14FastMap with 3 elements = {[42] = "foo", [43] = "bar",
# CHECK:   [44] = "baz"}
p m_fval
# CHECK: folly::F14FastMap with 3 elements = {[9] = 0, [8] = 1,
# CHECK:   [7] = 2}

p s_node
# CHECK: folly::F14NodeSet with 3 elements = {"foo", "bar", "baz"}
p s_node_large
# CHECK: folly::F14NodeSet with 20 elements = {{{[0-9, ]*[[:space:]][0-9, ]*}}}
p s_val
# CHECK: folly::F14ValueSet with 3 elements = {"foo", "bar", "baz"}
p s_val_i
# CHECK: folly::F14ValueSet with 20 elements = {{{[0-9, ]*[[:space:]][0-9, ]*}}}
p s_vec
# CHECK: folly::F14VectorSet with 3 elements = {"foo", "bar", "baz"}
p s_fvec
# CHECK: folly::F14FastSet with 3 elements = {"foo", "bar", "baz"}
p s_fval
# CHECK: folly::F14FastSet with 3 elements = {42, 43, 44}
p s_fval_typedef
# CHECK: folly::F14FastSet with 3 elements = {45, 46, 47}
p const_ref
# CHECK: folly::F14FastSet with 3 elements = {42, 43, 44}
