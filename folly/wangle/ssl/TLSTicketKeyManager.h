/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/async/SSLContext.h>
#include <folly/io/async/EventBase.h>

namespace folly {

#ifndef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
class TLSTicketKeyManager {};
#else
class SSLStats;
/**
 * The TLSTicketKeyManager handles TLS ticket key encryption and decryption in
 * a way that facilitates sharing the ticket keys across a range of servers.
 * Hash chaining is employed to achieve frequent key rotation with minimal
 * configuration change.  The scheme is as follows:
 *
 * The manager is supplied with three lists of seeds (old, current and new).
 * The config should be updated with new seeds periodically (e.g., daily).
 * 3 config changes are recommended to achieve the smoothest seed rotation
 * eg:
 *     1. Introduce new seed in the push prior to rotation
 *     2. Rotation push
 *     3. Remove old seeds in the push following rotation
 *
 * Multiple seeds are supported but only a single seed is required.
 *
 * Generating encryption keys from the seed works as follows.  For a given
 * seed, hash forward N times where N is currently the constant 1.
 * This is the base key.  The name of the base key is the first 4
 * bytes of hash(hash(seed), N).  This is copied into the first 4 bytes of the
 * TLS ticket key name field.
 *
 * For each new ticket encryption, the manager generates a random 12 byte salt.
 * Hash the salt and the base key together to form the encryption key for
 * that ticket.  The salt is included in the ticket's 'key name' field so it
 * can be used to derive the decryption key.  The salt is copied into the second
 * 8 bytes of the TLS ticket key name field.
 *
 * A key is valid for decryption for the lifetime of the instance.
 * Sessions will be valid for less time than that, which results in an extra
 * symmetric decryption to discover the session is expired.
 *
 * A TLSTicketKeyManager should be used in only one thread, and should have
 * a 1:1 relationship with the SSLContext provided.
 *
 */
class TLSTicketKeyManager : private boost::noncopyable {
 public:

  explicit TLSTicketKeyManager(folly::SSLContext* ctx,
                               SSLStats* stats);

  virtual ~TLSTicketKeyManager();

  /**
   * SSL callback to set up encryption/decryption context for a TLS Ticket Key.
   *
   * This will be supplied to the SSL library via
   * SSL_CTX_set_tlsext_ticket_key_cb.
   */
  static int callback(SSL* ssl, unsigned char* keyName,
                      unsigned char* iv,
                      EVP_CIPHER_CTX* cipherCtx,
                      HMAC_CTX* hmacCtx, int encrypt);

  /**
   * Initialize the manager with three sets of seeds.  There must be at least
   * one current seed, or the manager will revert to the default SSL behavior.
   *
   * @param oldSeeds Seeds previously used which can still decrypt.
   * @param currentSeeds Seeds to use for new ticket encryptions.
   * @param newSeeds Seeds which will be used soon, can be used to decrypt
   *                 in case some servers in the cluster have already rotated.
   */
  bool setTLSTicketKeySeeds(const std::vector<std::string>& oldSeeds,
                            const std::vector<std::string>& currentSeeds,
                            const std::vector<std::string>& newSeeds);

 private:
  enum TLSTicketSeedType {
    SEED_OLD = 0,
    SEED_CURRENT,
    SEED_NEW
  };

  /* The seeds supplied by the configuration */
  struct TLSTicketSeed {
    std::string seed_;
    TLSTicketSeedType type_;
    unsigned char seedName_[SHA256_DIGEST_LENGTH];
  };

  struct TLSTicketKeySource {
    int32_t hashCount_;
    std::string keyName_;
    TLSTicketSeedType type_;
    unsigned char keySource_[SHA256_DIGEST_LENGTH];
  };

  /**
   * Method to setup encryption/decryption context for a TLS Ticket Key
   *
   * OpenSSL documentation is thin on the return value semantics.
   *
   * For encrypt=1, return < 0 on error, >= 0 for successfully initialized
   * For encrypt=0, return < 0 on error, 0 on key not found
   *                 1 on key found, 2 renew_ticket
   *
   * renew_ticket means a new ticket will be issued.  We could return this value
   * when receiving a ticket encrypted with a key derived from an OLD seed.
   * However, session_timeout seconds after deploying with a seed
   * rotated from  CURRENT -> OLD, there will be no valid tickets outstanding
   * encrypted with the old key.  This grace period means no unnecessary
   * handshakes will be performed.  If the seed is believed compromised, it
   * should NOT be configured as an OLD seed.
   */
  int processTicket(SSL* ssl, unsigned char* keyName,
                    unsigned char* iv,
                    EVP_CIPHER_CTX* cipherCtx,
                    HMAC_CTX* hmacCtx, int encrypt);

  // Creates the name for the nth key generated from seed
  std::string makeKeyName(TLSTicketSeed* seed, uint32_t n,
                          unsigned char* nameBuf);

  /**
   * Creates the key hashCount hashes from the given seed and inserts it in
   * ticketKeys.  A naked pointer to the key is returned for additional
   * processing if needed.
   */
  TLSTicketKeySource* insertNewKey(TLSTicketSeed* seed, uint32_t hashCount,
                                   TLSTicketKeySource* prevKeySource);

  /**
   * hashes input N times placing result in output, which must be at least
   * SHA256_DIGEST_LENGTH long.
   */
  void hashNth(const unsigned char* input, size_t input_len,
               unsigned char* output, uint32_t n);

  /**
   * Adds the given seed to the manager
   */
  TLSTicketSeed* insertSeed(const std::string& seedInput,
                            TLSTicketSeedType type);

  /**
   * Locate a key for encrypting a new ticket
   */
  TLSTicketKeySource* findEncryptionKey();

  /**
   * Locate a key for decrypting a ticket with the given keyName
   */
  TLSTicketKeySource* findDecryptionKey(unsigned char* keyName);

  /**
   * Derive a unique key from the parent key and the salt via hashing
   */
  void makeUniqueKeys(unsigned char* parentKey, size_t keyLen,
                      unsigned char* salt, unsigned char* output);

  /**
   * For standalone decryption utility
   */
  friend int decrypt_fb_ticket(folly::TLSTicketKeyManager* manager,
                               const std::string& testTicket,
                               SSL_SESSION **psess);

  typedef std::vector<std::unique_ptr<TLSTicketSeed>> TLSTicketSeedList;
  typedef std::map<std::string, std::unique_ptr<TLSTicketKeySource> >
    TLSTicketKeyMap;
  typedef std::vector<TLSTicketKeySource *> TLSActiveKeyList;

  TLSTicketSeedList ticketSeeds_;
  // All key sources that can be used for decryption
  TLSTicketKeyMap ticketKeys_;
  // Key sources that can be used for encryption
  TLSActiveKeyList activeKeys_;

  folly::SSLContext* ctx_;
  uint32_t randState_;
  SSLStats* stats_{nullptr};

  static int32_t sExDataIndex_;
};
#endif
}
