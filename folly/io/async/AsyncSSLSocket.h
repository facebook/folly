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

#include <iomanip>

#include <folly/Optional.h>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncPipe.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/CertificateIdentityVerifier.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/TimeoutManager.h>
#include <folly/io/async/ssl/OpenSSLUtils.h>
#include <folly/io/async/ssl/SSLErrors.h>
#include <folly/io/async/ssl/TLSDefinitions.h>
#include <folly/lang/Bits.h>
#include <folly/portability/OpenSSL.h>
#include <folly/portability/Sockets.h>
#include <folly/ssl/OpenSSLPtrTypes.h>
#include <folly/ssl/SSLSession.h>
#include <folly/ssl/SSLSessionManager.h>

namespace folly {

class AsyncSSLSocketConnector;

/**
 * A class for performing asynchronous I/O on an SSL connection.
 *
 * AsyncSSLSocket allows users to asynchronously wait for data on an
 * SSL connection, and to asynchronously send data.
 *
 * The APIs for reading and writing are intentionally asymmetric.
 * Waiting for data to read is a persistent API: a callback is
 * installed, and is notified whenever new data is available.  It
 * continues to be notified of new events until it is uninstalled.
 *
 * AsyncSSLSocket does not provide read timeout functionality,
 * because it typically cannot determine when the timeout should be
 * active.  Generally, a timeout should only be enabled when
 * processing is blocked waiting on data from the remote endpoint.
 * For server connections, the timeout should not be active if the
 * server is currently processing one or more outstanding requests for
 * this connection.  For client connections, the timeout should not be
 * active if there are no requests pending on the connection.
 * Additionally, if a client has multiple pending requests, it will
 * ususally want a separate timeout for each request, rather than a
 * single read timeout.
 *
 * The write API is fairly intuitive: a user can request to send a
 * block of data, and a callback will be informed once the entire
 * block has been transferred to the kernel, or on error.
 * AsyncSSLSocket does provide a send timeout, since most callers
 * want to give up if the remote end stops responding and no further
 * progress can be made sending the data.
 */
class AsyncSSLSocket : public AsyncSocket {
 public:
  typedef std::unique_ptr<AsyncSSLSocket, Destructor> UniquePtr;
  using X509_deleter = folly::static_function_deleter<X509, &X509_free>;

  class HandshakeCB {
   public:
    virtual ~HandshakeCB() = default;

    /**
     * handshakeVer() is invoked during handshaking to give the
     * application chance to validate it's peer's certificate.
     *
     * Note that OpenSSL performs only rudimentary internal
     * consistency verification checks by itself. Any other validation
     * like whether or not the certificate was issued by a trusted CA.
     * The default implementation of this callback mimics what what
     * OpenSSL does internally if SSL_VERIFY_PEER is set with no
     * verification callback.
     *
     * See the passages on verify_callback in SSL_CTX_set_verify(3)
     * for more details.
     */
    virtual bool handshakeVer(
        AsyncSSLSocket* /*sock*/,
        bool preverifyOk,
        X509_STORE_CTX* /*ctx*/) noexcept {
      return preverifyOk;
    }

    /**
     * handshakeSuc() is called when a new SSL connection is
     * established, i.e., after SSL_accept/connect() returns successfully.
     *
     * The HandshakeCB will be uninstalled before handshakeSuc()
     * is called.
     *
     * @param sock  SSL socket on which the handshake was initiated
     */
    virtual void handshakeSuc(AsyncSSLSocket* sock) noexcept = 0;

    /**
     * handshakeErr() is called if an error occurs while
     * establishing the SSL connection.
     *
     * The HandshakeCB will be uninstalled before handshakeErr()
     * is called.
     *
     * @param sock  SSL socket on which the handshake was initiated
     * @param ex  An exception representing the error.
     */
    virtual void handshakeErr(
        AsyncSSLSocket* sock, const AsyncSocketException& ex) noexcept = 0;
  };

  class Timeout : public AsyncTimeout {
   public:
    Timeout(AsyncSSLSocket* sslSocket, EventBase* eventBase)
        : AsyncTimeout(eventBase), sslSocket_(sslSocket) {}

    bool scheduleTimeout(TimeoutManager::timeout_type timeout) {
      timeout_ = timeout;
      return AsyncTimeout::scheduleTimeout(timeout);
    }

    bool scheduleTimeout(uint32_t timeoutMs) {
      return scheduleTimeout(std::chrono::milliseconds{timeoutMs});
    }

    TimeoutManager::timeout_type getTimeout() { return timeout_; }

    void timeoutExpired() noexcept override {
      sslSocket_->timeoutExpired(timeout_);
    }

   private:
    AsyncSSLSocket* sslSocket_;
    TimeoutManager::timeout_type timeout_;
  };

  /**
   * A class to wait for asynchronous operations with OpenSSL 1.1.0
   */
  class DefaultOpenSSLAsyncFinishCallback : public ReadCallback {
   public:
    DefaultOpenSSLAsyncFinishCallback(
        AsyncPipeReader::UniquePtr reader,
        AsyncSSLSocket* sslSocket,
        DestructorGuard dg)
        : pipeReader_(std::move(reader)),
          sslSocket_(sslSocket),
          dg_(std::move(dg)) {}

    ~DefaultOpenSSLAsyncFinishCallback() override {
      pipeReader_->setReadCB(nullptr);
      sslSocket_->setAsyncOperationFinishCallback(nullptr);
    }

    void readDataAvailable(size_t len) noexcept override {
      CHECK_EQ(len, 1u);
      sslSocket_->restartSSLAccept();
      pipeReader_->setReadCB(nullptr);
      sslSocket_->setAsyncOperationFinishCallback(nullptr);
    }

    void getReadBuffer(void** bufReturn, size_t* lenReturn) noexcept override {
      *bufReturn = &byte_;
      *lenReturn = 1;
    }

    void readEOF() noexcept override {}

    void readErr(const folly::AsyncSocketException&) noexcept override {}

   private:
    uint8_t byte_{0};
    AsyncPipeReader::UniquePtr pipeReader_;
    AsyncSSLSocket* sslSocket_{nullptr};
    DestructorGuard dg_;
  };

  /**
   * Struct to consolidate constructor arguments.
   */
  struct Options {
    // If this verifier is set, it's used during the TLS handshake. It will be
    // invoked to verify the peer's end-entity leaf certificate after OpenSSL's
    // chain validation and after calling the HandshakeCB's handshakeVer() and
    // only if these are successful.
    std::shared_ptr<CertificateIdentityVerifier> verifier;
    bool deferSecurityNegotiation{};
    bool isServer{};
  };

  /**
   * Initialize this AsyncSSLSocket object with the given Options.
   *
   * @param options optional arguments for this AsyncSSLSocket instance
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      EventBase* evb,
      Options&& options);

  /**
   * Initialize this AsyncSSLSocket object with the given Options from an
   * already connected AsyncSocket.
   *
   * @param options optional arguments for this AsyncSSLSocket instance
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      AsyncSocket::UniquePtr oldAsyncSocket,
      Options&& options);

  /**
   * Create a client AsyncSSLSocket
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      EventBase* evb,
      bool deferSecurityNegotiation = false);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * socket file descriptor.
   *
   * Note that while AsyncSSLSocket enables TCP_NODELAY for sockets it creates
   * when connecting, it does not change the socket options when given an
   * existing file descriptor.  If callers want TCP_NODELAY enabled when using
   * this version of the constructor, they need to explicitly call
   * setNoDelay(true) after the constructor returns.
   *
   * @param ctx             SSL context for this connection.
   * @param evb EventBase that will manage this socket.
   * @param fd  File descriptor to take over (should be a connected socket).
   * @param server Is socket in server mode?
   * @param deferSecurityNegotiation
   *          unencrypted data can be sent before sslConn/Accept
   * @param peerAddress optional peer address (eg: returned from accept).  If
   *          nullptr, AsyncSocket will lazily attempt to determine it from fd
   *          via a system call
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      EventBase* evb,
      NetworkSocket fd,
      bool server = true,
      bool deferSecurityNegotiation = false,
      const SocketAddress* peerAddress = nullptr);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * AsyncSocket.
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      AsyncSocket* oldAsyncSocket,
      bool server = true,
      bool deferSecurityNegotiation = false);

  /**
   * Create a server/client AsyncSSLSocket from an already connected
   * AsyncSocket.
   */
  AsyncSSLSocket(
      std::shared_ptr<folly::SSLContext> ctx,
      AsyncSocket::UniquePtr oldAsyncSocket,
      bool server = true,
      bool deferSecurityNegotiation = false);

  /**
   * Helper function to create a server/client shared_ptr<AsyncSSLSocket>.
   */
  static UniquePtr newSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      EventBase* evb,
      NetworkSocket fd,
      bool server = true,
      bool deferSecurityNegotiation = false,
      const folly::SocketAddress* peerAddress = nullptr) {
    return AsyncSSLSocket::UniquePtr(new AsyncSSLSocket(
        ctx, evb, fd, server, deferSecurityNegotiation, peerAddress));
  }

  /**
   * Helper function to create a client shared_ptr<AsyncSSLSocket>.
   */
  static UniquePtr newSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      EventBase* evb,
      bool deferSecurityNegotiation = false) {
    return AsyncSSLSocket::UniquePtr(
        new AsyncSSLSocket(ctx, evb, deferSecurityNegotiation));
  }

#if FOLLY_OPENSSL_HAS_SNI
  /**
   * Create a client AsyncSSLSocket with tlsext_servername in
   * the Client Hello message.
   */
  AsyncSSLSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      EventBase* evb,
      const std::string& serverName,
      bool deferSecurityNegotiation = false);

  /**
   * Create a client AsyncSSLSocket from an already connected
   * socket file descriptor.
   *
   * Note that while AsyncSSLSocket enables TCP_NODELAY for sockets it creates
   * when connecting, it does not change the socket options when given an
   * existing file descriptor.  If callers want TCP_NODELAY enabled when using
   * this version of the constructor, they need to explicitly call
   * setNoDelay(true) after the constructor returns.
   *
   * @param ctx  SSL context for this connection.
   * @param evb  EventBase that will manage this socket.
   * @param fd   File descriptor to take over (should be a connected socket).
   * @param serverName tlsext_hostname that will be sent in ClientHello.
   * @param deferSecurityNegotiation
   *          unencrypted data can be sent before sslConn/Accept
   * @param peerAddress optional peer address (eg: returned from accept).  If
   *          nullptr, AsyncSocket will lazily attempt to determine it from fd
   *          via a system call
   */
  AsyncSSLSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      EventBase* evb,
      NetworkSocket fd,
      const std::string& serverName,
      bool deferSecurityNegotiation = false,
      const SocketAddress* peerAddr = nullptr);

  static UniquePtr newSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      EventBase* evb,
      const std::string& serverName,
      bool deferSecurityNegotiation = false) {
    return AsyncSSLSocket::UniquePtr(
        new AsyncSSLSocket(ctx, evb, serverName, deferSecurityNegotiation));
  }
#endif // FOLLY_OPENSSL_HAS_SNI

  /**
   * TODO: implement support for SSL renegotiation.
   *
   * This involves proper handling of the SSL_ERROR_WANT_READ/WRITE
   * code as a result of SSL_write/read(), instead of returning an
   * error. In that case, the READ/WRITE event should be registered,
   * and a flag (e.g., writeBlockedOnRead) should be set to indiciate
   * the condition. In the next invocation of read/write callback, if
   * the flag is on, performWrite()/performRead() should be called in
   * addition to the normal call to performRead()/performWrite(), and
   * the flag should be reset.
   */

  // Inherit AsyncTransport methods from AsyncSocket except the
  // following.
  // See the documentation in AsyncTransport.h
  // TODO: implement graceful shutdown in close()
  // TODO: implement detachSSL() that returns the SSL connection
  void closeNow() override;
  void shutdownWrite() override;
  void shutdownWriteNow() override;
  bool readable() const override;
  bool good() const override;
  bool connecting() const override;
  std::string getApplicationProtocol() const noexcept override;
  void setSupportedApplicationProtocols(
      const std::vector<std::string>& supportedProtocols);

  std::string getSecurityProtocol() const override {
    if (sslState_ == STATE_UNENCRYPTED) {
      return "";
    }
    return "TLS";
  }

  std::unique_ptr<folly::IOBuf> getExportedKeyingMaterial(
      folly::StringPiece label,
      std::unique_ptr<IOBuf> context,
      uint16_t length) const override;

  void setEorTracking(bool track) override;
  size_t getRawBytesWritten() const override;
  size_t getRawBytesReceived() const override;

  // End of methods inherited from AsyncTransport

  /**
   * Enable ByteEvents for this socket.
   *
   * ByteEvents cannot be enabled if TLS 1.0 or earlier is in use, as these
   * client implementations often have trouble handling cases where a TLS
   * record is split across multiple packets.
   */
  void enableByteEvents() override;

  void enableClientHelloParsing();

  /**
   * Accept an SSL connection on the socket.
   *
   * The callback will be invoked and uninstalled when an SSL
   * connection has been established on the underlying socket.
   * The value of verifyPeer determines the client verification method.
   * By default, its set to use the value in the underlying context
   *
   * @param callback callback object to invoke on success/failure
   * @param timeout timeout for this function in milliseconds, or 0 for no
   *                timeout
   * @param verifyPeer  SSLVerifyPeerEnum uses the options specified in the
   *                context by default, can be set explcitly to override the
   *                method in the context
   */
  virtual void sslAccept(
      HandshakeCB* callback,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
      const folly::SSLContext::SSLVerifyPeerEnum& verifyPeer =
          folly::SSLContext::SSLVerifyPeerEnum::USE_CTX);

  /**
   * Invoke SSL accept following an asynchronous session cache lookup
   */
  void restartSSLAccept();

  /**
   * Connect to the given address, invoking callback when complete or on error
   *
   * Note timeout applies to TCP + SSL connection time
   */
  void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout = 0,
      const SocketOptionMap& options = emptySocketOptionMap,
      const folly::SocketAddress& bindAddr = anyAddress(),
      const std::string& ifName = "") noexcept override;

  /**
   * A variant of connect that allows the caller to specify
   * the timeout for the regular connect and the ssl connect
   * separately.
   * connectTimeout is specified as the time to establish a TCP
   * connection.
   * totalConnectTimeout defines the
   * time it takes from starting the TCP connection to the time
   * the ssl connection is established. The reason the timeout is
   * defined this way is because user's rarely need to specify the SSL
   * timeout independently of the connect timeout. It allows us to
   * bound the time for a connect and SSL connection in
   * a finer grained manner than if timeout was just defined
   * independently for SSL.
   */
  virtual void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      std::chrono::milliseconds connectTimeout,
      std::chrono::milliseconds totalConnectTimeout,
      const SocketOptionMap& options = emptySocketOptionMap,
      const folly::SocketAddress& bindAddr = anyAddress(),
      const std::string& ifName = "") noexcept;

  using AsyncSocket::connect;

  /**
   * If a connect request is in-flight, cancels it and closes the socket
   * immediately. Otherwise, this is a no-op.
   *
   * This does not invoke any connection related callbacks. Call this to
   * prevent any connect callback while cleaning up, etc.
   */
  void cancelConnect() override;

  /**
   * Initiate an SSL connection on the socket
   * The callback will be invoked and uninstalled when an SSL connection
   * has been establshed on the underlying socket.
   * The verification option verifyPeer is applied if it's passed explicitly.
   * If it's not, the options in SSLContext set on the underlying SSLContext
   * are applied.
   *
   * @param callback callback object to invoke on success/failure
   * @param timeout timeout for this function in milliseconds, or 0 for no
   *                timeout
   * @param verifyPeer  SSLVerifyPeerEnum uses the options specified in the
   *                context by default, can be set explcitly to override the
   *                method in the context. If verification is turned on sets
   *                SSL_VERIFY_PEER and invokes
   *                HandshakeCB::handshakeVer().
   */
  virtual void sslConn(
      HandshakeCB* callback,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
      const folly::SSLContext::SSLVerifyPeerEnum& verifyPeer =
          folly::SSLContext::SSLVerifyPeerEnum::USE_CTX);

  enum SSLStateEnum {
    STATE_UNINIT,
    STATE_UNENCRYPTED,
    STATE_ACCEPTING,
    STATE_ASYNC_PENDING,
    STATE_CONNECTING,
    STATE_ESTABLISHED,
    STATE_REMOTE_CLOSED, /// remote end closed; we can still write
    STATE_CLOSING, ///< close() called, but waiting on writes to complete
    /// close() called with pending writes, before connect() has completed
    STATE_CONNECTING_CLOSING,
    STATE_CLOSED,
    STATE_ERROR
  };

  SSLStateEnum getSSLState() const { return sslState_; }

  /**
   * Retrieve the SSL session associated with this established connection.
   *
   * The SSL Session object is a copyable, opaque token that can be set on other
   * unconnected AsyncSSLSockets. If AsyncSSLSocket::connect() is called with a
   * previous session set, TLS resumption will be attempted.
   */
  std::shared_ptr<ssl::SSLSession> getSSLSession();

  /**
   * Get a handle to the SSL struct.
   */
  const SSL* getSSL() const;

  /**
   * Sets the SSL session that will be attempted for TLS resumption.
   */
  void setSSLSession(std::shared_ptr<ssl::SSLSession> session);

  /**
   * Note: This function exists for compatibility reasons. It is strongly
   * recommended to use setSSLSession instead. After setRawSSLSession is
   * called, subsequent calls to getSSLSession on the socket will return null.
   *
   * Set the SSL session to be used during sslConn.
   * If the caller wishes to resume the session in TLS 1.3, the caller
   * is responsible for ensuring that the session is resumable.
   * If the session is not resumable, then a full handshake will be performed.
   */
  void setRawSSLSession(folly::ssl::SSLSessionUniquePtr session);

  /**
   * Get the name of the protocol selected by the client during
   * Application Layer Protocol Negotiation (ALPN)
   *
   * Throw an exception if openssl does not support NPN
   *
   * @param protoName      Name of the protocol (not guaranteed to be
   *                       null terminated); will be set to nullptr if
   *                       the client did not negotiate a protocol.
   *                       Note: the AsyncSSLSocket retains ownership
   *                       of this string.
   * @param protoNameLen   Length of the name.
   * @param protoType      Whether this was an NPN or ALPN negotiation
   */
  virtual void getSelectedNextProtocol(
      const unsigned char** protoName, unsigned* protoLen) const;

  /**
   * Get the name of the protocol selected by the client during
   * Next Protocol Negotiation (NPN) or Application Layer Protocol Negotiation
   * (ALPN)
   *
   * @param protoName      Name of the protocol (not guaranteed to be
   *                       null terminated); will be set to nullptr if
   *                       the client did not negotiate a protocol.
   *                       Note: the AsyncSSLSocket retains ownership
   *                       of this string.
   * @param protoNameLen   Length of the name.
   * @param protoType      Whether this was an NPN or ALPN negotiation
   * @return false if openssl does not support NPN
   */
  virtual bool getSelectedNextProtocolNoThrow(
      const unsigned char** protoName, unsigned* protoLen) const;

  /**
   * Determine if the session specified during setSSLSession was reused
   * or if the server rejected it and issued a new session.
   */
  virtual bool getSSLSessionReused() const;

  /**
   * true if the session was resumed using session ID
   */
  bool sessionIDResumed() const { return sessionIDResumed_; }

  void setSessionIDResumed(bool resumed) { sessionIDResumed_ = resumed; }

  /**
   * Get the negociated cipher name for this SSL connection.
   * Returns the cipher used or the constant value "NONE" when no SSL session
   * has been established.
   */
  virtual const char* getNegotiatedCipherName() const;

  /**
   * Get the server name for this SSL connection. Returns the SNI sent in the
   * ClientHello, if enableClientHelloParsing() was called.
   *
   * Returns the server name used or the constant value "NONE" when no SSL
   * session has been established.
   * If openssl has no SNI support, throw AsyncSocketException.
   */
  const char* getSSLServerName() const;

  /**
   * Get the server name for this SSL connection.
   * Returns the server name used or the constant value "NONE" when no SSL
   * session has been established.
   * If openssl has no SNI support, return "NONE"
   */
  const char* getSSLServerNameNoThrow() const;

  /**
   * Get the SSL version for this connection.
   * Possible return values are SSL2_VERSION, SSL3_VERSION, TLS1_VERSION,
   * with hexa representations 0x200, 0x300, 0x301,
   * or 0 if no SSL session has been established.
   */
  int getSSLVersion() const;

  /**
   * Get the signature algorithm used in the cert that is used for this
   * connection.
   */
  const char* getSSLCertSigAlgName() const;

  /**
   * Get the certificate size used for this SSL connection.
   */
  int getSSLCertSize() const;

  void attachEventBase(EventBase* eventBase) override {
    AsyncSocket::attachEventBase(eventBase);
    handshakeTimeout_.attachEventBase(eventBase);
    connectionTimeout_.attachEventBase(eventBase);
  }

  void detachEventBase() override {
    AsyncSocket::detachEventBase();
    handshakeTimeout_.detachEventBase();
    connectionTimeout_.detachEventBase();
  }

  bool isDetachable() const override {
    return AsyncSocket::isDetachable() && !handshakeTimeout_.isScheduled();
  }

  virtual void attachTimeoutManager(TimeoutManager* manager) {
    handshakeTimeout_.attachTimeoutManager(manager);
  }

  virtual void detachTimeoutManager() {
    handshakeTimeout_.detachTimeoutManager();
  }

#if OPENSSL_VERSION_NUMBER >= 0x009080bfL
  /**
   * This function will set the SSL context for this socket to the
   * argument. This should only be used on client SSL Sockets that have
   * already called detachSSLContext();
   */
  void attachSSLContext(const std::shared_ptr<folly::SSLContext>& ctx);

  /**
   * Detaches the SSL context for this socket.
   */
  void detachSSLContext();
#endif

  /**
   * Returns the original folly::SSLContext associated with this socket.
   *
   * Suitable for use in AsyncSSLSocket constructor to construct a new
   * AsyncSSLSocket using an existing socket's context.
   *
   * switchServerSSLContext() does not affect this return value.
   */
  const std::shared_ptr<folly::SSLContext>& getSSLContext() const {
    return ctx_;
  }

#if FOLLY_OPENSSL_HAS_SNI
  /**
   * Switch the SSLContext to continue the SSL handshake.
   * It can only be used in server mode.
   */
  void switchServerSSLContext(
      const std::shared_ptr<folly::SSLContext>& handshakeCtx);

  /**
   * Did server recognize/support the tlsext_hostname in Client Hello?
   * It can only be used in client mode.
   *
   * @return true - tlsext_hostname is matched by the server
   *         false - tlsext_hostname is not matched or
   *                 is not supported by server
   */
  bool isServerNameMatch() const;

  /**
   * Set the SNI hostname that we'll advertise to the server in the
   * ClientHello message.
   */
  void setServerName(std::string serverName) noexcept;
#endif // FOLLY_OPENSSL_HAS_SNI

  void timeoutExpired(std::chrono::milliseconds timeout) noexcept;

  /**
   * Get the list of supported ciphers sent by the client in the client's
   * preference order.
   */
  void getSSLClientCiphers(
      std::string& clientCiphers, bool convertToString = true) const;

  /**
   * Get the list of compression methods sent by the client in TLS Hello.
   */
  std::string getSSLClientComprMethods() const;

  /**
   * Get the list of TLS extensions sent by the client in the TLS Hello.
   */
  std::string getSSLClientExts() const;

  std::string getSSLClientSigAlgs() const;

  /**
   * Get the list of versions in the supported versions extension (used to
   * negotiate TLS 1.3).
   */
  std::string getSSLClientSupportedVersions() const;

  std::string getSSLAlertsReceived() const;

  /*
   * Save an optional alert message generated during certificate verify
   */
  void setSSLCertVerificationAlert(std::string alert);

  std::string getSSLCertVerificationAlert() const;

  /**
   * Get the list of shared ciphers between the server and the client.
   * Works well for only SSLv2, not so good for SSLv3 or TLSv1.
   */
  void getSSLSharedCiphers(std::string& sharedCiphers) const;

  /**
   * Get the list of ciphers supported by the server in the server's
   * preference order.
   */
  void getSSLServerCiphers(std::string& serverCiphers) const;

  /**
   * Get the list of next protocols sent from the client. The protocols are
   * directly as the client passed them and may be arbitrary byte sequences
   * of arbitrary length.
   */
  const std::vector<std::string>& getClientAlpns() const;

  /**
   * Method to check if peer verfication is set.
   *
   * @return true if peer verification is required.
   */
  bool needsPeerVerification() const;

  static int getSSLExDataIndex();
  static AsyncSSLSocket* getFromSSL(const SSL* ssl);
  static int bioWrite(BIO* b, const char* in, int inl);
  static int bioRead(BIO* b, char* out, int outl);
  void resetClientHelloParsing(SSL* ssl);
  static void parseClientAlpns(
      AsyncSSLSocket* sock,
      folly::io::Cursor& cursor,
      uint16_t& extensionDataLength);
  static void clientHelloParsingCallback(
      int written,
      int version,
      int contentType,
      const void* buf,
      size_t len,
      SSL* ssl,
      void* arg);
  static const char* getSSLServerNameFromSSL(SSL* ssl);

  // For unit-tests
  ssl::ClientHelloInfo* getClientHelloInfo() const {
    return clientHelloInfo_.get();
  }

  /**
   * Returns the time taken to complete a handshake.
   */
  virtual std::chrono::nanoseconds getHandshakeTime() const {
    return handshakeEndTime_ - handshakeStartTime_;
  }

  void setMinWriteSize(size_t minWriteSize) { minWriteSize_ = minWriteSize; }

  size_t getMinWriteSize() const { return minWriteSize_; }

  const AsyncTransportCertificate* getPeerCertificate() const override;
  const AsyncTransportCertificate* getSelfCertificate() const override;

  /**
   * Force AsyncSSLSocket object to cache local and peer socket addresses.
   * If called with "true" before connect() this function forces full local
   * and remote socket addresses to be cached in the socket object and available
   * through getLocalAddress()/getPeerAddress() methods even after the socket is
   * closed.
   */
  void forceCacheAddrOnFailure(bool force) { cacheAddrOnFailure_ = force; }

  const std::string& getSessionKey() const { return sessionKey_; }

  void setSessionKey(std::string sessionKey) {
    sessionKey_ = std::move(sessionKey);
  }

  void setCertCacheHit(bool hit) { certCacheHit_ = hit; }

  bool getCertCacheHit() const { return certCacheHit_; }

  bool sessionResumptionAttempted() const {
    return sessionResumptionAttempted_;
  }

  /**
   * If the SSL socket was used to connect as well
   * as establish an SSL connection, this gives the total
   * timeout for the connect + SSL connection that was
   * set.
   */
  std::chrono::milliseconds getTotalConnectTimeout() const {
    return totalConnectTimeout_;
  }

  // This can be called for OpenSSL 1.1.0 async operation finishes
  void setAsyncOperationFinishCallback(std::unique_ptr<ReadCallback> cb) {
    asyncOperationFinishCallback_ = std::move(cb);
  }

  // Only enable if security negotiation is deferred
  // zero copy is not supported by openssl.
  bool setZeroCopy(bool enable) override {
    if (sslState_ == STATE_UNENCRYPTED) {
      return AsyncSocket::setZeroCopy(enable);
    }
    return false;
  }

 private:
  /**
   * Handle the return from invoking SSL_accept
   */
  void handleReturnFromSSLAccept(int ret);

  void init();

  // Need to clean this up during a cancel if callback hasn't fired yet.
  AsyncSSLSocketConnector* allocatedConnectCallback_;

 protected:
  /**
   * Protected destructor.
   *
   * Users of AsyncSSLSocket must never delete it directly.  Instead, invoke
   * destroy() instead.  (See the documentation in DelayedDestruction.h for
   * more details.)
   */
  ~AsyncSSLSocket() override;

  // Inherit event notification methods from AsyncSocket except
  // the following.
  void handleRead() noexcept override;
  void handleWrite() noexcept override;
  void handleAccept() noexcept;
  void handleConnect() noexcept override;

  void invalidState(HandshakeCB* callback);
  bool willBlock(
      int ret, int* sslErrorOut, unsigned long* errErrorOut) noexcept;

  void checkForImmediateRead() noexcept override;
  // AsyncSocket calls this at the wrong time for SSL
  void handleInitialReadWrite() noexcept override {}

  WriteResult interpretSSLError(int rc, int error);
  ReadResult performRead(void** buf, size_t* buflen, size_t* offset) override;
  ReadResult performReadv(struct iovec* iovs, size_t num) override;
  WriteResult performWrite(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten) override;

  ssize_t performWriteIovec(
      const iovec* vec,
      uint32_t count,
      WriteFlags flags,
      uint32_t* countWritten,
      uint32_t* partialWritten);

  // Virtual wrapper around SSL_write, solely for testing/mockability
  virtual int sslWriteImpl(SSL* ssl, const void* buf, int n) {
    return SSL_write(ssl, buf, n);
  }

  // Virtual wrapper around SSL_get_error, solely for testing/mockability
  virtual int sslGetErrorImpl(const SSL* s, int ret_code) {
    return SSL_get_error(s, ret_code);
  }

  /**
   * Apply verification options passed to sslConn/sslAccept or those set
   * in the underlying SSLContext object.
   *
   * @param ssl pointer to the SSL object on which verification options will be
   * applied. If verifyPeer_ was explicitly set either via sslConn/sslAccept,
   * those options override the settings in the underlying SSLContext.
   */
  bool applyVerificationOptions(const ssl::SSLUniquePtr& ssl);

  /**
   * Sets up SSL with a custom write bio which intercepts all writes.
   *
   * @return true, if succeeds and false if there is an error creating the bio.
   */
  bool setupSSLBio();

  // Inherit error handling methods from AsyncSocket, plus the following.
  void failHandshake(const char* fn, const AsyncSocketException& ex);

  void invokeHandshakeErr(const AsyncSocketException& ex);
  void invokeHandshakeCB();

  void invokeConnectErr(const AsyncSocketException& ex) override;
  void invokeConnectSuccess() override;
  void scheduleConnectTimeout() override;

  void startSSLConnect();

  static void sslInfoCallback(const SSL* ssl, int where, int ret);

  // Whether the current write to the socket should use MSG_MORE.
  bool corkCurrentWrite_{false};
  // SSL related members.
  bool server_{false};
  // Used to prevent client-initiated renegotiation.  Note that AsyncSSLSocket
  // doesn't fully support renegotiation, so we could just fail all attempts
  // to enforce this.  Once it is supported, we should make it an option
  // to disable client-initiated renegotiation.
  bool handshakeComplete_{false};
  bool renegotiateAttempted_{false};
  SSLStateEnum sslState_{STATE_UNINIT};
  std::shared_ptr<folly::SSLContext> ctx_;
  // Callback for SSL_accept() or SSL_connect()
  HandshakeCB* handshakeCallback_{nullptr};
  std::shared_ptr<CertificateIdentityVerifier> certificateIdentityVerifier_;
  ssl::SSLUniquePtr ssl_;
  Timeout handshakeTimeout_;
  Timeout connectionTimeout_;

  // WriteFlags last passed to performWrite
  WriteFlags currWriteFlags_{};

  // Number of bytes to write before final byte
  // See AsyncSSLSocket::performWrite for details
  folly::Optional<size_t> currBytesToFinalByte_;

  // Try to avoid calling SSL_write() for buffers smaller than this.
  // It doesn't take effect when it is 0.
  size_t minWriteSize_{1500};

#if FOLLY_OPENSSL_HAS_SNI
  std::shared_ptr<folly::SSLContext> handshakeCtx_;
  std::string tlsextHostname_;
#endif

  // a key that can be used for caching the established session
  std::string sessionKey_;

  folly::SSLContext::SSLVerifyPeerEnum verifyPeer_{
      folly::SSLContext::SSLVerifyPeerEnum::USE_CTX};

  // Callback for SSL_CTX_set_verify()
  static int sslVerifyCallback(int preverifyOk, X509_STORE_CTX* ctx);

  bool parseClientHello_{false};
  bool cacheAddrOnFailure_{false};
  bool certCacheHit_{false};
  std::unique_ptr<ssl::ClientHelloInfo> clientHelloInfo_;
  std::vector<std::pair<char, StringPiece>> alertsReceived_;

  // Time taken to complete the ssl handshake.
  std::chrono::steady_clock::time_point handshakeStartTime_;
  std::chrono::steady_clock::time_point handshakeEndTime_;
  std::chrono::milliseconds handshakeConnectTimeout_{0};
  std::chrono::milliseconds totalConnectTimeout_{0};

  std::string sslVerificationAlert_;

  std::string encodedAlpn_;

  bool sessionResumptionAttempted_{false};
  // whether the SSL session was resumed using session ID or not
  bool sessionIDResumed_{false};
  // This can be called for OpenSSL 1.1.0 async operation finishes
  std::unique_ptr<ReadCallback> asyncOperationFinishCallback_;
  // Whether this socket is currently waiting on SSL_accept
  bool waitingOnAccept_{false};
  // Manages the session for the socket
  folly::ssl::SSLSessionManager sslSessionManager_;
};

} // namespace folly
