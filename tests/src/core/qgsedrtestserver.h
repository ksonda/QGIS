/***************************************************************************
    qgsedrtestserver.h
    ---------------------
    begin                : September 2026
    copyright            : (C) 2026 by Kyle Onda
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSEDRTESTSERVER_H
#define QGSEDRTESTSERVER_H
#include <functional>
#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

using namespace Qt::StringLiterals;
// Each test owns a loopback server. The client's event loop services these sockets,
// so no external service, Python interpreter, or fixed listening port is required.
class QgsEdrTestServer : public QTcpServer
{
  public:
    struct Request
    {
        QByteArray method;
        QUrl url;
        QMap<QByteArray, QByteArray> headers = {};
        QByteArray body;
    };
    struct Response
    {
        QJsonObject object;
        int status = 200;
        QMap<QByteArray, QByteArray> headers = {};
        bool pending = false;
    };
    QList<Request> requests;
    std::function<Response( const Request & )> respond;

    QgsEdrTestServer()
    {
      connect( this, &QTcpServer::newConnection, this, [this] {
        while ( hasPendingConnections() )
        {
          QTcpSocket *socket = nextPendingConnection();
          auto buffer = std::make_shared<QByteArray>();
          connect( socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater );
          connect( socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            const int end = buffer->indexOf( "\r\n\r\n" );
            if ( end < 0 )
              return;
            const QList<QByteArray> lines = buffer->left( end ).split( '\n' );
            const QList<QByteArray> first = lines.first().trimmed().split( ' ' );
            Request request;
            request.method = first.at( 0 );
            request.url = QUrl::fromEncoded( first.at( 1 ) );
            for ( int index = 1; index < lines.size(); ++index )
            {
              const int colon = lines[index].indexOf( ':' );
              if ( colon > 0 )
                request.headers.insert( lines[index].left( colon ).trimmed().toLower(), lines[index].mid( colon + 1 ).trimmed() );
            }
            const int length = request.headers.value( "content-length" ).toInt();
            if ( buffer->size() < end + 4 + length )
              return;
            request.body = buffer->mid( end + 4, length );
            requests.append( request );
            const Response response = respond( request );
            if ( response.pending )
              return;
            const QByteArray body = response.status == 204 ? QByteArray() : QJsonDocument( response.object ).toJson( QJsonDocument::Compact );
            QByteArray header = "HTTP/1.1 "
                                + QByteArray::number( response.status )
                                + " Result\r\nContent-Type: application/vnd.cov+json\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: "
                                + QByteArray::number( body.size() )
                                + "\r\n";
            for ( auto it = response.headers.constBegin(); it != response.headers.constEnd(); ++it )
              header += it.key() + ": " + it.value() + "\r\n";
            socket->write( header + "\r\n" + body );
            socket->disconnectFromHost();
          } );
        }
      } );
      listen( QHostAddress::LocalHost, 0 );
    }

    QUrl url( const QString &path = QString() ) const { return QUrl( u"http://127.0.0.1:%1%2"_s.arg( serverPort() ).arg( path ) ); }
};


#endif
