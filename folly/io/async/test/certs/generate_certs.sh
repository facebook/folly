#!/bin/bash -ue
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

CERT_LIFETIME_DAYS=${CERT_LIFETIME_DAYS:-36500}

extensions() {
  cat << EOF
[ca]
basicConstraints        = critical, CA:TRUE
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always, issuer:always
keyUsage                = critical, cRLSign, digitalSignature, keyCertSign

[client]
basicConstraints        = critical, CA:FALSE
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always
keyUsage                = critical, nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage        = critical, clientAuth

[server]
basicConstraints        = critical, CA:FALSE
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always
keyUsage                = critical, nonRepudiation, digitalSignature, keyEncipherment, keyAgreement
extendedKeyUsage        = critical, serverAuth
subjectAltName          = IP:127.0.0.1,IP:::1

[client_and_server]
basicConstraints        = critical, CA:FALSE
subjectKeyIdentifier    = hash
authorityKeyIdentifier  = keyid:always
keyUsage                = critical, nonRepudiation, digitalSignature, keyEncipherment, keyAgreement
extendedKeyUsage        = critical, serverAuth, clientAuth
subjectAltName          = IP:127.0.0.1,IP:::1
EOF
}

die() {
  echo "$@" >&2
  exit 1
}

generate_key() {
  keytype="$1"
  modulus_bits="$2"
  outfile="$3"

  [ "$keytype" != "rsa" ] && die "unsupported key type"

  openssl genrsa -out "$outfile" "$modulus_bits" 2>/dev/null
}

mkcsr() {
  local key="$1"
  local name="$2"
  local output="$3"

  openssl req -new \
    -sha256 \
    -key "$key" \
    -keyform PEM \
    -subj "/CN=$name/" \
    -outform PEM \
    -out "$output" \

}

selfsign() {
  local incsr="$1"
  local inkey="$2"
  local outfile="$3"

  openssl x509 \
    -req \
    -set_serial 1 \
    -signkey "$inkey" \
    -inform PEM \
    -in "$incsr" \
    -outform PEM \
    -out "$outfile" \
    -days "$CERT_LIFETIME_DAYS" \
    -extfile <(extensions) \
    -extensions "ca" \

}

sign() {
  csr="$1"
  cacert="$2"
  cakey="$3"
  serial="$4"
  extensions="$5"
  outfile="$6"


  openssl x509 \
    -req \
    -set_serial "$serial" \
    -CA "$cacert" \
    -CAkey "$cakey" \
    -inform PEM \
    -in "$csr" \
    -outform PEM \
    -out "$outfile" \
    -days "$CERT_LIFETIME_DAYS" \
    -extfile <(extensions) \
    -extensions "$extensions" \

}

# Client CA
generate_key rsa 2048 client_ca_key.pem
mkcsr client_ca_key.pem "Asox Certification Authority" client_ca.csr.pem
selfsign client_ca.csr.pem client_ca_key.pem client_ca_cert.pem

# Client Intermediate CA
generate_key rsa 2048 client_intermediate_ca_key.pem
mkcsr client_intermediate_ca_key.pem "Intermediate CA" client_intermediate_ca.csr.pem
sign client_intermediate_ca.csr.pem client_ca_cert.pem client_ca_key.pem 2 ca client_intermediate_ca.pem

# Directly signed client cert (client_key.pem)
generate_key rsa 2048 client_key.pem
mkcsr client_key.pem "testuser1" client.csr.pem
sign client.csr.pem client_ca_cert.pem client_ca_key.pem 100 client client_cert.pem

# Client signed by an intermediate
generate_key rsa 2048 clienti_key.pem
mkcsr clienti_key.pem "Leaf Certificate" clienti.csr.pem
sign clienti.csr.pem client_intermediate_ca.pem client_intermediate_ca_key.pem 200 client clienti_cert.pem
cat clienti_cert.pem client_intermediate_ca.pem > client_chain.pem

# Server CA "Thrift Certificate Authority"
generate_key rsa 2048 ca-key.pem
mkcsr ca-key.pem "Thrift Certificate Authority" ca.csr.pem
selfsign ca.csr.pem ca-key.pem ca-cert.pem

# Server cert "Asox Company"
generate_key rsa 2048 tests-key.pem
mkcsr tests-key.pem "Asox Company" tests.csr.pem
sign tests.csr.pem ca-cert.pem ca-key.pem 300 client_and_server tests-cert.pem

# Remove all of the CSRs but leave the keys in case you need to regenerate a
# different cert with the same key.
rm ./*.csr.pem
