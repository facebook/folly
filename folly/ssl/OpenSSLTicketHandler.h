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

#pragma once

#include <folly/portability/OpenSSL.h>

namespace folly {

/**
 * The OpenSSLTicketHandler handles TLS ticket encryption and decryption.
 *
 * This is meant to be used within an SSLContext to configure ticket resumption
 * for the SSL_CTX by leveraging the SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB API. Note
 * that the interface here is constrained by the API OpenSSL exposes and is
 * therefore not intended for other TLS implementations.
 *
 * An OpenSSLTicketHandler should be used in only one thread, and should be
 * unique to each SSLContext.
 */
class OpenSSLTicketHandler {
 public:
  virtual ~OpenSSLTicketHandler() = default;

  /**
   * Method to setup encryption/decryption context for a TLS Ticket.
   *
   * For encrypt=1, return < 0 on error, >= 0 for successfully initialized
   * For encrypt=0, return < 0 on error, 0 on key not found
   *                 1 on key found, 2 renew_ticket
   *
   * where renew_ticket means a new ticket will be issued.
   */
  virtual int ticketCallback(
      SSL* ssl,
      unsigned char* keyName,
      unsigned char* iv,
      EVP_CIPHER_CTX* cipherCtx,
      HMAC_CTX* hmacCtx,
      int encrypt) = 0;
};
} // namespace folly
