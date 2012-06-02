/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 *
 * Serialize and deserialize folly::dynamic values as JSON.
 *
 * Before you use this you should probably understand the basic
 * concepts in the JSON type system:
 *
 *    Value  : String | Bool | Null | Object | Array | Number
 *    String : UTF-8 sequence
 *    Object : (String, Value) pairs, with unique String keys
 *    Array  : ordered list of Values
 *    Null   : null
 *    Bool   : true | false
 *    Number : (representation unspecified)
 *
 * ... That's about it.  For more information see http://json.org or
 * look up RFC 4627.
 *
 * If your dynamic has anything illegal with regard to this type
 * system, the serializer will throw.
 *
 * @author Jordan DeLong <delong.j@fb.com>
 */

#ifndef FOLLY_JSON_H_
#define FOLLY_JSON_H_

#include "folly/dynamic.h"
#include "folly/FBString.h"
#include "folly/Range.h"

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace json {

  struct serialization_opts {
    explicit serialization_opts()
      : allow_non_string_keys(false)
      , javascript_safe(false)
      , pretty_formatting(false)
      , encode_non_ascii(false)
      , validate_utf8(false)
    {}

    // If true, keys in an object can be non-strings.  (In strict
    // JSON, object keys must be strings.)  This is used by dynamic's
    // operator<<.
    bool allow_non_string_keys;

    /*
     * If true, refuse to serialize 64-bit numbers that cannot be
     * precisely represented by fit a double---instead, throws an
     * exception if the document contains this.
     */
    bool javascript_safe;

    // If true, the serialized json will contain space and newlines to
    // try to be minimally "pretty".
    bool pretty_formatting;

    // If true, non-ASCII utf8 characters would be encoded as \uXXXX.
    bool encode_non_ascii;

    // Check that strings are valid utf8
    bool validate_utf8;
  };

  /*
   * Main JSON serialization routine taking folly::dynamic parameters.
   * For the most common use cases there are simpler functions in the
   * main folly namespace below.
   */
  fbstring serialize(dynamic const&, serialization_opts const&);

}

//////////////////////////////////////////////////////////////////////

/*
 * Parse a json blob out of a range and produce a dynamic representing
 * it.
 */
dynamic parseJson(StringPiece);

/*
 * Serialize a dynamic into a json string.
 */
fbstring toJson(dynamic const&);

/*
 * Same as the above, except format the json with some minimal
 * indentation.
 */
fbstring toPrettyJson(dynamic const&);

//////////////////////////////////////////////////////////////////////

}

#endif
