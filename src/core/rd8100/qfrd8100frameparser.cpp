/***************************************************************************
 qfrd8100frameparser.cpp - QfRd8100FrameParser

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

#include "qfrd8100frameparser.h"

#include <QLocale>
#include <QObject>

namespace
{
  // Field positions within "$RDMRX,…" sentences, field 0 being the sentence type.
  // Established from captures of an RD8100 PXLM (firmware 1003, ASCII protocol 2);
  // depth and current positions still need confirmation over a live conductor.
  constexpr int LOG_NUMBER_FIELD = 4;
  constexpr int FREQUENCY_TYPE_FIELD = 5;
  constexpr int FREQUENCY_FIELD = 6;
  constexpr int MODE_FIELD = 9;
  constexpr int DEPTH_UNIT_FIELD = 11;
  constexpr int DEPTH_FIELD = 12;
  constexpr int CURRENT_UNIT_FIELD = 13;
  constexpr int CURRENT_FIELD = 14;

  QVariant toNumber( const QString &value )
  {
    bool ok = false;
    const double number = QLocale::c().toDouble( value, &ok );
    return ok ? QVariant( number ) : QVariant();
  }

  QVariant nonEmpty( const QString &value )
  {
    return value.isEmpty() ? QVariant() : QVariant( value );
  }
} // namespace

QfRd8100FrameParser::Result QfRd8100FrameParser::feed( const QByteArray &data )
{
  Result result;
  mBuffer.append( data );

  qsizetype lineEnd = mBuffer.indexOf( '\n' );
  while ( lineEnd >= 0 )
  {
    const QByteArray line = mBuffer.left( lineEnd );
    mBuffer.remove( 0, lineEnd + 1 );
    processLine( line, result );
    lineEnd = mBuffer.indexOf( '\n' );
  }

  if ( mBuffer.size() > MAXIMUM_SENTENCE_LENGTH )
  {
    result.errors << QObject::tr( "Discarded %1 bytes received without line ending" ).arg( mBuffer.size() );
    mBuffer.clear();
  }

  return result;
}

QfRd8100FrameParser::Result QfRd8100FrameParser::flush()
{
  Result result;
  if ( !mBuffer.isEmpty() )
  {
    const QByteArray remaining = mBuffer;
    mBuffer.clear();
    processLine( remaining, result );
  }
  return result;
}

void QfRd8100FrameParser::reset()
{
  mBuffer.clear();
}

void QfRd8100FrameParser::processLine( QByteArray line, Result &result )
{
  line = line.trimmed();
  if ( line.isEmpty() )
  {
    return;
  }

  const qsizetype start = line.indexOf( '$' );
  if ( start < 0 )
  {
    result.errors << QObject::tr( "Ignored data without sentence start: %1" ).arg( QString::fromLatin1( line ) );
    return;
  }

  QString error;
  const QVariantMap measurement = parseSentence( line.mid( start ), &error );
  if ( measurement.isEmpty() )
  {
    result.errors << error;
  }
  else
  {
    result.measurements << measurement;
  }
}

bool QfRd8100FrameParser::isChecksumValid( const QByteArray &sentence )
{
  const qsizetype star = sentence.lastIndexOf( '*' );
  if ( !sentence.startsWith( '$' ) || star < 1 || sentence.size() != star + 3 )
  {
    return false;
  }

  bool ok = false;
  const int expected = sentence.mid( star + 1, 2 ).toInt( &ok, 16 );
  if ( !ok )
  {
    return false;
  }

  quint8 checksum = 0;
  for ( qsizetype i = 1; i < star; ++i )
  {
    checksum ^= static_cast<quint8>( sentence.at( i ) );
  }
  return checksum == expected;
}

QVariantMap QfRd8100FrameParser::parseSentence( const QByteArray &sentence, QString *error )
{
  auto fail = [error, &sentence]( const QString &reason ) {
    if ( error )
    {
      *error = QStringLiteral( "%1: %2" ).arg( reason, QString::fromLatin1( sentence ) );
    }
    return QVariantMap();
  };

  if ( !sentence.startsWith( '$' ) )
  {
    return fail( QObject::tr( "Missing sentence start" ) );
  }

  const qsizetype star = sentence.lastIndexOf( '*' );
  if ( star < 0 || sentence.size() != star + 3 )
  {
    return fail( QObject::tr( "Missing or malformed checksum" ) );
  }

  if ( !isChecksumValid( sentence ) )
  {
    return fail( QObject::tr( "Invalid checksum" ) );
  }

  const QStringList fields = QString::fromLatin1( sentence.mid( 1, star - 1 ) ).split( QLatin1Char( ',' ) );
  if ( !fields.at( 0 ).startsWith( QLatin1String( "RD" ) ) )
  {
    return fail( QObject::tr( "Unsupported sentence type" ) );
  }

  auto field = [&fields]( int index ) -> QString {
    return index < fields.size() ? fields.at( index ).trimmed() : QString();
  };

  QVariantMap measurement;
  measurement.insert( QStringLiteral( "raw" ), QString::fromLatin1( sentence ) );
  measurement.insert( QStringLiteral( "sentence_type" ), fields.at( 0 ) );
  measurement.insert( QStringLiteral( "fields" ), fields );
  measurement.insert( QStringLiteral( "field_count" ), fields.size() );
  for ( int i = 0; i < fields.size(); ++i )
  {
    measurement.insert( QStringLiteral( "field_%1" ).arg( i ), fields.at( i ) );
  }

  bool ok = false;
  const int logNumber = field( LOG_NUMBER_FIELD ).toInt( &ok );
  measurement.insert( QStringLiteral( "log_number" ), ok ? QVariant( logNumber ) : QVariant() );

  measurement.insert( QStringLiteral( "frequency_type" ), nonEmpty( field( FREQUENCY_TYPE_FIELD ) ) );
  QString frequency = field( FREQUENCY_FIELD );
  if ( frequency.endsWith( QLatin1String( "Hz" ), Qt::CaseInsensitive ) )
  {
    frequency.chop( 2 );
  }
  measurement.insert( QStringLiteral( "frequency" ), toNumber( frequency ) );
  measurement.insert( QStringLiteral( "mode" ), nonEmpty( field( MODE_FIELD ) ) );

  const QString depthUnit = field( DEPTH_UNIT_FIELD );
  const QVariant depth = toNumber( field( DEPTH_FIELD ) );
  measurement.insert( QStringLiteral( "depth_unit" ), nonEmpty( depthUnit ) );
  measurement.insert( QStringLiteral( "depth" ), depth );
  measurement.insert( QStringLiteral( "depth_m" ), depth.isValid() ? toMeters( depth.toDouble(), depthUnit ) : QVariant() );

  measurement.insert( QStringLiteral( "current_unit" ), nonEmpty( field( CURRENT_UNIT_FIELD ) ) );
  measurement.insert( QStringLiteral( "current" ), toNumber( field( CURRENT_FIELD ) ) );

  return measurement;
}

QVariant QfRd8100FrameParser::toMeters( double value, const QString &unit )
{
  const QString normalized = unit.trimmed().toLower();
  if ( normalized == QLatin1String( "m" ) )
  {
    return value;
  }
  if ( normalized == QLatin1String( "cm" ) )
  {
    return value / 100.0;
  }
  if ( normalized == QLatin1String( "mm" ) )
  {
    return value / 1000.0;
  }
  if ( normalized == QLatin1String( "inches" ) || normalized == QLatin1String( "inch" ) || normalized == QLatin1String( "in" ) )
  {
    return value * 0.0254;
  }
  if ( normalized == QLatin1String( "feet" ) || normalized == QLatin1String( "foot" ) || normalized == QLatin1String( "ft" ) )
  {
    return value * 0.3048;
  }
  return QVariant();
}
