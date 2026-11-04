/***************************************************************************
 qfrd8100receiver.h - QfRd8100Receiver

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
#ifndef QFRD8100RECEIVER_H
#define QFRD8100RECEIVER_H

#include "qfrd8100frameparser.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtBluetooth/QBluetoothLocalDevice>
#include <QtBluetooth/QBluetoothServer>
#include <QtBluetooth/QBluetoothServiceInfo>
#include <QtBluetooth/QBluetoothSocket>

#include <memory>

/**
 * Receives survey measurements from a Radiodetection RD8100 locator.
 *
 * The locator initiates a Bluetooth classic connection to the paired phone each
 * time a measurement is saved, sends a single ASCII sentence and disconnects.
 * This receiver therefore publishes a Serial Port Profile service and waits for
 * incoming connections, instead of connecting to the device like GNSS receivers.
 * \ingroup core
 */
class QfRd8100Receiver : public QObject
{
    Q_OBJECT

    Q_PROPERTY( bool active READ active WRITE setActive NOTIFY activeChanged )
    Q_PROPERTY( Status status READ status NOTIFY statusChanged )
    Q_PROPERTY( QString lastError READ lastError NOTIFY lastErrorChanged )
    Q_PROPERTY( QVariantMap lastMeasurement READ lastMeasurement NOTIFY lastMeasurementChanged )
    Q_PROPERTY( int measurementCount READ measurementCount NOTIFY measurementCountChanged )

  public:
    enum class Status
    {
      Inactive,    //!< Not listening
      Listening,   //!< Service published, waiting for the locator
      Connected,   //!< The locator is connected and sending data
      Unavailable, //!< Bluetooth is off, missing or not permitted; retried automatically
    };
    Q_ENUM( Status )

    explicit QfRd8100Receiver( QObject *parent = nullptr );
    ~QfRd8100Receiver() override;

    //! Returns TRUE when the receiver should be listening for the locator
    bool active() const { return mActive; }

    //! Sets whether the receiver should be listening for the locator
    void setActive( bool active );

    //! Returns the current listening status
    Status status() const { return mStatus; }

    //! Returns the last error message, empty if none
    QString lastError() const { return mLastError; }

    /**
     * Returns the last measurement received, or an empty map when none is pending.
     * Keys are documented in QfRd8100FrameParser::parseSentence(), with the addition
     * of received_at, device_address and device_name.
     */
    QVariantMap lastMeasurement() const { return mLastMeasurement; }

    //! Returns the number of measurements received since the receiver was created
    int measurementCount() const { return mMeasurementCount; }

    //! Clears the pending measurement, typically once it was used to create a feature
    Q_INVOKABLE void clearLastMeasurement();

    //! Service name published over Bluetooth
    static const QString SERVICE_NAME;

  signals:
    void activeChanged();
    void statusChanged();
    void lastErrorChanged();
    void lastMeasurementChanged();
    void measurementCountChanged();

    //! Emitted for every valid measurement received
    void measurementReceived( const QVariantMap &measurement );

    //! Emitted when received data could not be parsed, \a reason contains the offending data
    void frameRejected( const QString &reason );

  private:
    void start();
    void stop();
    void scheduleRestart();
    void handleNewConnection();
    void handleSocketData( QBluetoothSocket *socket );
    void handleSocketDisconnected( QBluetoothSocket *socket );
    void handleResult( QBluetoothSocket *socket, const QfRd8100FrameParser::Result &result );
    void setStatus( Status status );
    void setLastError( const QString &error );

    bool mActive = false;
    bool mPermissionRequested = false;
    Status mStatus = Status::Inactive;
    QString mLastError;
    QVariantMap mLastMeasurement;
    int mMeasurementCount = 0;

    std::unique_ptr<QBluetoothLocalDevice> mLocalDevice;
    QBluetoothServer *mServer = nullptr;
    QBluetoothServiceInfo mServiceInfo;
    QHash<QBluetoothSocket *, QfRd8100FrameParser> mParsers;
    QTimer mRestartTimer;
    int mRestartAttempts = 0;
};

#endif // QFRD8100RECEIVER_H
