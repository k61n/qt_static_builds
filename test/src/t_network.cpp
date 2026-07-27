#include "report.h"
#include "tests.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

void testNetwork()
{
    report::section(QStringLiteral("Network: sockets, DNS, TLS backend and HTTP stack"));

    // --- addresses and interfaces -------------------------------------------
    {
        QHostAddress v4(QStringLiteral("192.168.1.10"));
        QHostAddress v6(QStringLiteral("2001:db8::1"));
        report::check(QStringLiteral("IPv4 address parsing"),
                      v4.protocol() == QAbstractSocket::IPv4Protocol
                              && v4.toString() == QStringLiteral("192.168.1.10"));
        report::check(QStringLiteral("IPv6 address parsing"),
                      v6.protocol() == QAbstractSocket::IPv6Protocol);
        report::check(QStringLiteral("loopback detection"),
                      QHostAddress(QHostAddress::LocalHost).isLoopback());
        report::check(QStringLiteral("subnet matching"),
                      v4.isInSubnet(QHostAddress(QStringLiteral("192.168.1.0")), 24));

        const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        report::info(QStringLiteral("network interfaces"), QString::number(interfaces.size()));
        bool loopbackFound = false;
        for (const QNetworkInterface &interface : interfaces) {
            if (interface.flags().testFlag(QNetworkInterface::IsLoopBack))
                loopbackFound = true;
            if (interface.flags().testFlag(QNetworkInterface::IsUp)
                && !interface.addressEntries().isEmpty()) {
                report::listItem(QStringLiteral("%1: %2").arg(
                        interface.humanReadableName(),
                        interface.addressEntries().first().ip().toString()));
            }
        }
        report::check(QStringLiteral("interface enumeration finds a loopback device"),
                      loopbackFound);
        report::info(QStringLiteral("local host name"), QHostInfo::localHostName());
        report::check(QStringLiteral("QHostInfo::localHostName"),
                      !QHostInfo::localHostName().isEmpty());
    }

    // --- TCP over loopback ---------------------------------------------------
    {
        QTcpServer server;
        if (report::check(QStringLiteral("QTcpServer listens on a loopback port"),
                          server.listen(QHostAddress::LocalHost, 0), server.errorString())) {
            report::info(QStringLiteral("server port"), QString::number(server.serverPort()));

            QTcpSocket client;
            client.connectToHost(QHostAddress::LocalHost, server.serverPort());
            const bool connected = client.waitForConnected(10000);
            report::check(QStringLiteral("QTcpSocket connects"), connected, client.errorString());

            if (connected && server.waitForNewConnection(10000)) {
                QTcpSocket *peer = server.nextPendingConnection();
                report::check(QStringLiteral("server accepts the connection"), peer != nullptr);

                client.write(QByteArrayLiteral("ping\n"));
                client.flush();
                report::check(QStringLiteral("server receives data"),
                              peer->waitForReadyRead(10000)
                                      && peer->readAll() == QByteArrayLiteral("ping\n"));

                peer->write(QByteArrayLiteral("pong\n"));
                peer->flush();
                report::check(QStringLiteral("client receives the reply"),
                              client.waitForReadyRead(10000)
                                      && client.readAll() == QByteArrayLiteral("pong\n"));

                report::info(QStringLiteral("peer address"),
                             QStringLiteral("%1:%2").arg(client.peerAddress().toString())
                                     .arg(client.peerPort()));
                peer->disconnectFromHost();
                peer->deleteLater();
            } else if (connected) {
                report::check(QStringLiteral("server accepts the connection"), false,
                              QStringLiteral("waitForNewConnection timed out"));
            }
            client.close();
            server.close();
        }
    }

    // --- UDP over loopback ---------------------------------------------------
    {
        QUdpSocket receiver;
        if (report::check(QStringLiteral("QUdpSocket binds to a loopback port"),
                          receiver.bind(QHostAddress::LocalHost, 0), receiver.errorString())) {
            QUdpSocket sender;
            const QByteArray datagram = QByteArrayLiteral("udp payload");
            const qint64 written = sender.writeDatagram(datagram, QHostAddress::LocalHost,
                                                        receiver.localPort());
            report::check(QStringLiteral("datagram is sent"), written == datagram.size(),
                          sender.errorString());

            if (report::check(QStringLiteral("datagram arrives"),
                              receiver.waitForReadyRead(10000), receiver.errorString())) {
                QByteArray buffer(int(receiver.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress from;
                quint16 fromPort = 0;
                receiver.readDatagram(buffer.data(), buffer.size(), &from, &fromPort);
                report::check(QStringLiteral("datagram payload matches"), buffer == datagram,
                              QString::fromLatin1(buffer));
            }
        }
    }

    // --- TLS backend ---------------------------------------------------------
    {
        report::info(QStringLiteral("QSslSocket::supportsSsl"), QSslSocket::supportsSsl());
        report::info(QStringLiteral("TLS library build version"),
                     QSslSocket::sslLibraryBuildVersionString());
        report::info(QStringLiteral("TLS library runtime version"),
                     QSslSocket::sslLibraryVersionString());
        const QList<QString> backends = QSslSocket::availableBackends();
        report::info(QStringLiteral("available TLS backends"),
                     backends.isEmpty() ? QStringLiteral("(none)")
                                        : backends.join(QStringLiteral(", ")));
        report::info(QStringLiteral("active TLS backend"), QSslSocket::activeBackend());
        report::soft(QStringLiteral("a TLS backend is available in this static build"),
                     QSslSocket::supportsSsl() && !backends.isEmpty(),
                     QStringLiteral("no usable TLS backend - HTTPS will not work"));
    }

    // --- HTTP stack against a local one-shot server --------------------------
    {
        QTcpServer server;
        if (report::check(QStringLiteral("local HTTP test server listens"),
                          server.listen(QHostAddress::LocalHost, 0), server.errorString())) {
            const QByteArray body = QByteArrayLiteral("Hello, World!");
            QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, body] {
                QTcpSocket *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, body] {
                    const QByteArray request = socket->readAll();
                    if (!request.contains(QByteArrayLiteral("\r\n\r\n")))
                        return;
                    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                                            "Content-Type: text/plain\r\n"
                                                            "Content-Length: ");
                    response += QByteArray::number(body.size());
                    response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                    response += body;
                    socket->write(response);
                    socket->flush();
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QTcpSocket::deleteLater);
            });

            QNetworkAccessManager manager;
            QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/hello")
                                                 .arg(server.serverPort())));
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("qt_static_test/1.0"));
            QNetworkReply *reply = manager.get(request);

            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            loop.exec();

            if (report::check(QStringLiteral("QNetworkAccessManager finished the request"),
                              reply->isFinished(), QStringLiteral("timed out"))) {
                const QByteArray received = reply->readAll();
                report::info(QStringLiteral("HTTP status"),
                             reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                                     .toString());
                report::info(QStringLiteral("HTTP body"),
                             report::ellipsize(QString::fromUtf8(received)));
                report::check(QStringLiteral("HTTP request succeeded"),
                              reply->error() == QNetworkReply::NoError, reply->errorString());
                report::check(QStringLiteral("HTTP status code and body"),
                              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                                              == 200
                                      && received == body,
                              QString::fromUtf8(received));
                report::check(QStringLiteral("response headers are parsed"),
                              reply->header(QNetworkRequest::ContentTypeHeader).toString()
                                      .startsWith(QStringLiteral("text/plain")));
            }
            reply->deleteLater();
            server.close();
        }
    }

    // --- DNS resolution (optional, requires a working resolver) --------------
    {
        QEventLoop loop;
        QHostInfo result;
        bool done = false;
        QHostInfo::lookupHost(QStringLiteral("localhost"), &loop,
                              [&](const QHostInfo &info) {
                                  result = info;
                                  done = true;
                                  loop.quit();
                              });
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        report::info(QStringLiteral("localhost resolves to"),
                     done && !result.addresses().isEmpty()
                             ? result.addresses().first().toString()
                             : QStringLiteral("(unresolved)"));
        report::soft(QStringLiteral("asynchronous DNS lookup of localhost"),
                     done && result.error() == QHostInfo::NoError
                             && !result.addresses().isEmpty(),
                     done ? result.errorString() : QStringLiteral("lookup timed out"));
    }
}