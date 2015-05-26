/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/ssl/TLSTicketKeyManager.h>

#include <folly/wangle/ssl/SSLStats.h>
#include <folly/wangle/ssl/SSLUtil.h>

#include <folly/String.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <folly/io/async/AsyncTimeout.h>

#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
using std::string;

namespace {

const int kTLSTicketKeyNameLen = 4;
const int kTLSTicketKeySaltLen = 12;

}

namespace folly {


// TLSTicketKeyManager Implementation
int32_t TLSTicketKeyManager::sExDataIndex_ = -1;

TLSTicketKeyManager::TLSTicketKeyManager(SSLContext* ctx, SSLStats* stats)
  : ctx_(ctx),
    randState_(0),
    stats_(stats) {
  SSLUtil::getSSLCtxExIndex(&sExDataIndex_);
  SSL_CTX_set_ex_data(ctx_->getSSLCtx(), sExDataIndex_, this);
}

TLSTicketKeyManager::~TLSTicketKeyManager() {
}

int
TLSTicketKeyManager::callback(SSL* ssl, unsigned char* keyName,
                              unsigned char* iv,
                              EVP_CIPHER_CTX* cipherCtx,
                              HMAC_CTX* hmacCtx, int encrypt) {
  TLSTicketKeyManager* manager = nullptr;
  SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
  manager = (TLSTicketKeyManager *)SSL_CTX_get_ex_data(ctx, sExDataIndex_);

  if (manager == nullptr) {
    LOG(FATAL) << "Null TLSTicketKeyManager in callback" ;
    return -1;
  }
  return manager->processTicket(ssl, keyName, iv, cipherCtx, hmacCtx, encrypt);
}

int
TLSTicketKeyManager::processTicket(SSL* ssl, unsigned char* keyName,
                                   unsigned char* iv,
                                   EVP_CIPHER_CTX* cipherCtx,
                                   HMAC_CTX* hmacCtx, int encrypt) {
  uint8_t salt[kTLSTicketKeySaltLen];
  uint8_t* saltptr = nullptr;
  uint8_t output[SHA256_DIGEST_LENGTH];
  uint8_t* hmacKey = nullptr;
  uint8_t* aesKey = nullptr;
  TLSTicketKeySource* key = nullptr;
  int result = 0;

  if (encrypt) {
    key = findEncryptionKey();
    if (key == nullptr) {
      // no keys available to encrypt
      VLOG(2) << "No TLS ticket key found";
      return -1;
    }
    VLOG(4) << "Encrypting new ticket with key name=" <<
      SSLUtil::hexlify(key->keyName_);

    // Get a random salt and write out key name
    RAND_pseudo_bytes(salt, (int)sizeof(salt));
    memcpy(keyName, key->keyName_.data(), kTLSTicketKeyNameLen);
    memcpy(keyName + kTLSTicketKeyNameLen, salt, kTLSTicketKeySaltLen);

    // Create the unique keys by hashing with the salt
    makeUniqueKeys(key->keySource_, sizeof(key->keySource_), salt, output);
    // This relies on the fact that SHA256 has 32 bytes of output
    // and that AES-128 keys are 16 bytes
    hmacKey = output;
    aesKey = output + SHA256_DIGEST_LENGTH / 2;

    // Initialize iv and cipher/mac CTX
    RAND_pseudo_bytes(iv, AES_BLOCK_SIZE);
    HMAC_Init_ex(hmacCtx, hmacKey, SHA256_DIGEST_LENGTH / 2,
                 EVP_sha256(), nullptr);
    EVP_EncryptInit_ex(cipherCtx, EVP_aes_128_cbc(), nullptr, aesKey, iv);

    result = 1;
  } else {
    key = findDecryptionKey(keyName);
    if (key == nullptr) {
      // no ticket found for decryption - will issue a new ticket
      if (VLOG_IS_ON(4)) {
        string skeyName((char *)keyName, kTLSTicketKeyNameLen);
        VLOG(4) << "Can't find ticket key with name=" <<
          SSLUtil::hexlify(skeyName)<< ", will generate new ticket";
      }

      result = 0;
    } else {
      VLOG(4) << "Decrypting ticket with key name=" <<
        SSLUtil::hexlify(key->keyName_);

      // Reconstruct the unique key via the salt
      saltptr = keyName + kTLSTicketKeyNameLen;
      makeUniqueKeys(key->keySource_, sizeof(key->keySource_), saltptr, output);
      hmacKey = output;
      aesKey = output + SHA256_DIGEST_LENGTH / 2;

      // Initialize cipher/mac CTX
      HMAC_Init_ex(hmacCtx, hmacKey, SHA256_DIGEST_LENGTH / 2,
                   EVP_sha256(), nullptr);
      EVP_DecryptInit_ex(cipherCtx, EVP_aes_128_cbc(), nullptr, aesKey, iv);

      result = 1;
    }
  }
  // result records whether a ticket key was found to decrypt this ticket,
  // not wether the session was re-used.
  if (stats_) {
    stats_->recordTLSTicket(encrypt, result);
  }

  return result;
}

bool
TLSTicketKeyManager::setTLSTicketKeySeeds(
    const std::vector<std::string>& oldSeeds,
    const std::vector<std::string>& currentSeeds,
    const std::vector<std::string>& newSeeds) {

  bool result = true;

  activeKeys_.clear();
  ticketKeys_.clear();
  ticketSeeds_.clear();
  const std::vector<string> *seedList = &oldSeeds;
  for (uint32_t i = 0; i < 3; i++) {
    TLSTicketSeedType type = (TLSTicketSeedType)i;
    if (type == SEED_CURRENT) {
      seedList = &currentSeeds;
    } else if (type == SEED_NEW) {
      seedList = &newSeeds;
    }

    for (const auto& seedInput: *seedList) {
      TLSTicketSeed* seed = insertSeed(seedInput, type);
      if (seed == nullptr) {
        result = false;
        continue;
      }
      insertNewKey(seed, 1, nullptr);
    }
  }
  if (!result) {
    VLOG(2) << "One or more seeds failed to decode";
  }

  if (ticketKeys_.size() == 0 || activeKeys_.size() == 0) {
    LOG(WARNING) << "No keys configured, falling back to default";
    SSL_CTX_set_tlsext_ticket_key_cb(ctx_->getSSLCtx(), nullptr);
    return false;
  }
  SSL_CTX_set_tlsext_ticket_key_cb(ctx_->getSSLCtx(),
                                   TLSTicketKeyManager::callback);

  return true;
}

string
TLSTicketKeyManager::makeKeyName(TLSTicketSeed* seed, uint32_t n,
                                 unsigned char* nameBuf) {
  SHA256_CTX ctx;

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, seed->seedName_, sizeof(seed->seedName_));
  SHA256_Update(&ctx, &n, sizeof(n));
  SHA256_Final(nameBuf, &ctx);
  return string((char *)nameBuf, kTLSTicketKeyNameLen);
}

TLSTicketKeyManager::TLSTicketKeySource*
TLSTicketKeyManager::insertNewKey(TLSTicketSeed* seed, uint32_t hashCount,
                                  TLSTicketKeySource* prevKey) {
  unsigned char nameBuf[SHA256_DIGEST_LENGTH];
  std::unique_ptr<TLSTicketKeySource> newKey(new TLSTicketKeySource);

  // This function supports hash chaining but it is not currently used.

  if (prevKey != nullptr) {
    hashNth(prevKey->keySource_, sizeof(prevKey->keySource_),
            newKey->keySource_, 1);
  } else {
    // can't go backwards or the current is missing, start from the beginning
    hashNth((unsigned char *)seed->seed_.data(), seed->seed_.length(),
            newKey->keySource_, hashCount);
  }

  newKey->hashCount_ = hashCount;
  newKey->keyName_ = makeKeyName(seed, hashCount, nameBuf);
  newKey->type_ = seed->type_;
  auto it = ticketKeys_.insert(std::make_pair(newKey->keyName_,
        std::move(newKey)));

  auto key = it.first->second.get();
  if (key->type_ == SEED_CURRENT) {
    activeKeys_.push_back(key);
  }
  VLOG(4) << "Adding key for " << hashCount << " type=" <<
    (uint32_t)key->type_ << " Name=" << SSLUtil::hexlify(key->keyName_);

  return key;
}

void
TLSTicketKeyManager::hashNth(const unsigned char* input, size_t input_len,
                             unsigned char* output, uint32_t n) {
  assert(n > 0);
  for (uint32_t i = 0; i < n; i++) {
    SHA256(input, input_len, output);
    input = output;
    input_len = SHA256_DIGEST_LENGTH;
  }
}

TLSTicketKeyManager::TLSTicketSeed *
TLSTicketKeyManager::insertSeed(const string& seedInput,
                                TLSTicketSeedType type) {
  TLSTicketSeed* seed = nullptr;
  string seedOutput;

  if (!folly::unhexlify<string, string>(seedInput, seedOutput)) {
    LOG(WARNING) << "Failed to decode seed type=" << (uint32_t)type <<
      " seed=" << seedInput;
    return seed;
  }

  seed = new TLSTicketSeed();
  seed->seed_ = seedOutput;
  seed->type_ = type;
  SHA256((unsigned char *)seedOutput.data(), seedOutput.length(),
         seed->seedName_);
  ticketSeeds_.push_back(std::unique_ptr<TLSTicketSeed>(seed));

  return seed;
}

TLSTicketKeyManager::TLSTicketKeySource *
TLSTicketKeyManager::findEncryptionKey() {
  TLSTicketKeySource* result = nullptr;
  // call to rand here is a bit hokey since it's not cryptographically
  // random, and is predictably seeded with 0.  However, activeKeys_
  // is probably not going to have very many keys in it, and most
  // likely only 1.
  size_t numKeys = activeKeys_.size();
  if (numKeys > 0) {
    result = activeKeys_[rand_r(&randState_) % numKeys];
  }
  return result;
}

TLSTicketKeyManager::TLSTicketKeySource *
TLSTicketKeyManager::findDecryptionKey(unsigned char* keyName) {
  string name((char *)keyName, kTLSTicketKeyNameLen);
  TLSTicketKeySource* key = nullptr;
  TLSTicketKeyMap::iterator mapit = ticketKeys_.find(name);
  if (mapit != ticketKeys_.end()) {
    key = mapit->second.get();
  }
  return key;
}

void
TLSTicketKeyManager::makeUniqueKeys(unsigned char* parentKey,
                                    size_t keyLen,
                                    unsigned char* salt,
                                    unsigned char* output) {
  SHA256_CTX hash_ctx;

  SHA256_Init(&hash_ctx);
  SHA256_Update(&hash_ctx, parentKey, keyLen);
  SHA256_Update(&hash_ctx, salt, kTLSTicketKeySaltLen);
  SHA256_Final(output, &hash_ctx);
}

} // namespace
#endif
