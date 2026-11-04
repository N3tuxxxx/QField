/***************************************************************************
 qfrd8100receiver.cpp - QfRd8100Receiver

 ---------------------
 begin                : 04.11.2026
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qfrd8100receiver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QMetaEnum>
#include <QPermissions>
#include <QtBluetooth/QBluetoothUuid>

#include <algorithm>

const QString QfRd8100Receiver::SERVICE_NAME = QStringLiteral( "QField RD8100" );

QfRd8100Receiver::QfRd8100Receiver( QObject *parent )
  : QObject( parent )
{
  mRestartTimer.setSingleShot( true );
  connect( &mRestartTimer, &QTimer::timeout, this, &QfRd8100Receiver::start );
}

QfRd8100Receiver::~QfRd8100Receiver()
{
  mActive = false;
  mRestartTimer.stop();
  stop();
}

void QfRd8100Receiver::setActive( bool active )
{
  if ( mActive == active )
  {
    return;
  }

  mActive = active;
  emit activeChanged();

  if ( mActive )
  {
    mRestartAttempts = 0;
    start();
  }
  else
  {
    mRestartTimer.stop();
    stop();
    setLastError( QString() );
    setStatus( Status::Inactive );
  }
}

void QfRd8100Receiver::clearLastMeasurement()
{
  if ( mLastMeasurement.isEmpty() )
  {
    return;
  }

  mLastMeasurement.clear();
  emit lastMeasurementChanged();
}

void QfRd8100Receiver::start()
{
  if ( !mActive || mServer )
  {
    return;
  }

#if QT_CONFIG( permissions )
  QBluetoothPermission permission;
  permission.setCommunicationModes( QBluetoothPermission::Access );
  switch ( QCoreApplication::instance()->checkPermission( permission ) )
  {
    case Qt::PermissionStatus::Undetermined:
      if ( !mPermissionRequested )
      {
        mPermissionRequested = true;
        QCoreApplication::instance()->requestPermission( permission, this, [this]( const QPermission &result ) {
          if ( result.status() == Qt::PermissionStatus::Granted )
          {
            start();
          }
          else
          {
            setLastError( tr( "Bluetooth permission denied" ) );
            setStatus( Status::Unavailable );
          }
        } );
      }
      return;

    case Qt::PermissionStatus::Denied:
      setLastError( tr( "Bluetooth permission denied" ) );
      setStatus( Status::Unavailable );
      return;

    case Qt::PermissionStatus::Granted:
      break;
  }
#endif

  if ( !mLocalDevice )
  {
    // Created once permission is granted, Android refuses adapter access before that
    mLocalDevice = std::make_unique<QBluetoothLocalDevice>();
    connect( mLocalDevice.get(), &QBluetoothLocalDevice::hostModeStateChanged, this, [this]( QBluetoothLocalDevice::HostMode mode ) {
      if ( !mActive )
      {
        return;
      }

      if ( mode == QBluetoothLocalDevice::HostPoweredOff )
      {
        qInfo() << "RD8100: Bluetooth switched off";
        mRestartTimer.stop();
        stop();
        setLastError( tr( "Bluetooth is switched off" ) );
        setStatus( Status::Unavailable );
      }
      else if ( !mServer )
      {
        qInfo() << "RD8100: Bluetooth switched on, publishing service again";
        mRestartAttempts = 0;
        start();
      }
    } );
  }

  if ( !mLocalDevice->isValid() )
  {
    setLastError( tr( "No Bluetooth adapter available" ) );
    setStatus( Status::Unavailable );
    return;
  }

  if ( mLocalDevice->hostMode() == QBluetoothLocalDevice::HostPoweredOff )
  {
    // Listening resumes from the host mode change handler
    setLastError( tr( "Bluetooth is switched off" ) );
    setStatus( Status::Unavailable );
    return;
  }

  mServer = new QBluetoothServer( QBluetoothServiceInfo::RfcommProtocol, this );
  // Only bonded devices may connect, the locator is paired with PIN 1234 beforehand
  mServer->setSecurityFlags( QBluetooth::Security::Secure );
  mServer->setMaxPendingConnections( 4 );
  connect( mServer, &QBluetoothServer::newConnection, this, &QfRd8100Receiver::handleNewConnection );
  connect( mServer, &QBluetoothServer::errorOccurred, this, [this]( QBluetoothServer::Error error ) {
    setLastError( tr( "Bluetooth server error (%1)" ).arg( QString::fromLatin1( QMetaEnum::fromType<QBluetoothServer::Error>().valueToKey( static_cast<int>( error ) ) ) ) );
    qInfo() << "RD8100:" << mLastError;
    stop();
    scheduleRestart();
  } );

  mServiceInfo = mServer->listen( QBluetoothUuid( QBluetoothUuid::ServiceClassUuid::SerialPort ), SERVICE_NAME );
  if ( !mServiceInfo.isValid() )
  {
    setLastError( tr( "Unable to publish the Bluetooth serial port service" ) );
    qInfo() << "RD8100:" << mLastError;
    stop();
    scheduleRestart();
    return;
  }

  qInfo() << "RD8100: Serial port service published, waiting for the locator";
  setLastError( QString() );
  setStatus( mParsers.isEmpty() ? Status::Listening : Status::Connected );
}

void QfRd8100Receiver::stop()
{
  for ( auto it = mParsers.constBegin(); it != mParsers.constEnd(); ++it )
  {
    QBluetoothSocket *socket = it.key();
    socket->disconnect( this );
    socket->abort();
    socket->deleteLater();
  }
  mParsers.clear();

  if ( mServer )
  {
    mServer->disconnect( this );
    if ( mServiceInfo.isRegistered() )
    {
      mServiceInfo.unregisterService();
    }
    mServer->close();
    // Deferred deletion, stop() may run from one of the server's own signals
    mServer->deleteLater();
    mServer = nullptr;
  }
  mServiceInfo = QBluetoothServiceInfo();
}

void QfRd8100Receiver::scheduleRestart()
{
  if ( !mActive )
  {
    return;
  }

  // 2 s, 4 s, 8 s, 16 s, then every 30 s
  const int delay = std::min( 30000, 2000 * ( 1 << std::min( mRestartAttempts, 4 ) ) );
  mRestartAttempts++;
  setStatus( Status::Unavailable );
  mRestartTimer.start( delay );
}

void QfRd8100Receiver::handleNewConnection()
{
  while ( mServer && mServer->hasPendingConnections() )
  {
    QBluetoothSocket *socket = mServer->nextPendingConnection();
    if ( !socket )
    {
      break;
    }

    qInfo() << "RD8100: Incoming connection from" << socket->peerName() << socket->peerAddress().toString();
    socket->setParent( this );
    mParsers.insert( socket, QfRd8100FrameParser() );
    connect( socket, &QBluetoothSocket::readyRead, this, [this, socket]() { handleSocketData( socket ); } );
    connect( socket, &QBluetoothSocket::disconnected, this, [this, socket]() { handleSocketDisconnected( socket ); } );
    connect( socket, &QBluetoothSocket::errorOccurred, this, [socket]( QBluetoothSocket::SocketError ) {
      qInfo() << "RD8100: Socket error:" << socket->errorString();
    } );
    setStatus( Status::Connected );

    // Data or disconnection may have happened before the connections above were made
    if ( socket->bytesAvailable() > 0 )
    {
      handleSocketData( socket );
    }
    if ( socket->state() == QBluetoothSocket::SocketState::UnconnectedState )
    {
      handleSocketDisconnected( socket );
    }
  }
}

void QfRd8100Receiver::handleSocketData( QBluetoothSocket *socket )
{
  auto it = mParsers.find( socket );
  if ( it == mParsers.end() )
  {
    return;
  }

  const QfRd8100FrameParser::Result result = it->feed( socket->readAll() );
  handleResult( socket, result );
}

void QfRd8100Receiver::handleSocketDisconnected( QBluetoothSocket *socket )
{
  if ( socket->bytesAvailable() > 0 )
  {
    handleSocketData( socket );
  }

  auto it = mParsers.find( socket );
  if ( it != mParsers.end() )
  {
    const QfRd8100FrameParser::Result result = it->flush();
    mParsers.erase( it );
    handleResult( socket, result );
  }

  qInfo() << "RD8100: Locator disconnected";
  socket->disconnect( this );
  socket->deleteLater();

  if ( mServer )
  {
    setStatus( mParsers.isEmpty() ? Status::Listening : Status::Connected );
  }
}

void QfRd8100Receiver::handleResult( QBluetoothSocket *socket, const QfRd8100FrameParser::Result &result )
{
  for ( const QString &error : result.errors )
  {
    qInfo() << "RD8100: Rejected data:" << error;
    emit frameRejected( error );
  }

  for ( QVariantMap measurement : result.measurements )
  {
    measurement.insert( QStringLiteral( "received_at" ), QDateTime::currentDateTime() );
    measurement.insert( QStringLiteral( "device_address" ), socket->peerAddress().toString() );
    measurement.insert( QStringLiteral( "device_name" ), socket->peerName() );

    mLastMeasurement = measurement;
    mMeasurementCount++;
    mRestartAttempts = 0;
    qInfo() << "RD8100: Measurement received:" << measurement.value( QStringLiteral( "raw" ) ).toString();

    emit lastMeasurementChanged();
    emit measurementCountChanged();
    emit measurementReceived( measurement );
  }
}

void QfRd8100Receiver::setStatus( Status status )
{
  if ( mStatus == status )
  {
    return;
  }

  mStatus = status;
  emit statusChanged();
}

void QfRd8100Receiver::setLastError( const QString &error )
{
  if ( mLastError == error )
  {
    return;
  }

  mLastError = error;
  emit lastErrorChanged();
}
