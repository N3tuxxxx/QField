/***************************************************************************
                        test_rd8100frameparser.cpp
                        --------------------------
  begin                : November 2026
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "catch2.h"
#include "qfrd8100frameparser.h"

#include <QFile>

// Sentences captured from an RD8100 PXLM (firmware 1003, ASCII protocol 2)
static const QByteArray inchesSentence( "$RDMRX,2,3,F03,0006,ACTIVE,32768Hz,8,---,L,P,inches,0,mA,0,dB,-60,dBuV,0,2,M_UNUSED,0,0.00,0,0,0,0,0,0,0,0,0,0.00,0.00000000,0.00000000,0,0,0.0,0.0,0,0.0,0,0,0,0,*1C" );
static const QByteArray sondeSentence( "$RDMRX,2,3,F03,0019,ACTIVE,32768Hz,8,---,S,P,cm,29,mA,0,dB,-60,dBuV,0,2,M_UNUSED,0,0.00,0,0,0,0,0,0,0,0,0,0.00,0.00000000,0.00000000,0,0,0.0,0.0,0,0.0,0,0,0,0,*22" );
static const QByteArray lineSentence( "$RDMRX,2,3,F03,0020,ACTIVE,65536Hz,18,---,L,S,cm,0,mA,33,dB,30,dBuV,0,2,M_UNUSED,0,0.00,0,0,0,0,0,0,0,0,0,0.00,0.00000000,0.00000000,0,0,0.0,0.0,0,0.0,0,0,0,0,*2D" );

TEST_CASE( "RD8100 checksum" )
{
  REQUIRE( QfRd8100FrameParser::isChecksumValid( inchesSentence ) );
  REQUIRE( QfRd8100FrameParser::isChecksumValid( sondeSentence ) );

  QByteArray corrupted = sondeSentence;
  corrupted.replace( ",29,", ",28," );
  REQUIRE_FALSE( QfRd8100FrameParser::isChecksumValid( corrupted ) );
  REQUIRE_FALSE( QfRd8100FrameParser::isChecksumValid( QByteArray( "$RDMRX,1*" ) ) );
  REQUIRE_FALSE( QfRd8100FrameParser::isChecksumValid( QByteArray( "RDMRX,1*00" ) ) );
}

TEST_CASE( "RD8100 sentence fields" )
{
  QString error;
  const QVariantMap sonde = QfRd8100FrameParser::parseSentence( sondeSentence, &error );
  REQUIRE( error.isEmpty() );
  REQUIRE( sonde.value( QStringLiteral( "raw" ) ).toString() == QString::fromLatin1( sondeSentence ) );
  REQUIRE( sonde.value( QStringLiteral( "sentence_type" ) ).toString() == QStringLiteral( "RDMRX" ) );
  REQUIRE( sonde.value( QStringLiteral( "field_count" ) ).toInt() == 46 );
  REQUIRE( sonde.value( QStringLiteral( "field_8" ) ).toString() == QStringLiteral( "---" ) );
  REQUIRE( sonde.value( QStringLiteral( "log_number" ) ).toInt() == 19 );
  REQUIRE( sonde.value( QStringLiteral( "frequency_type" ) ).toString() == QStringLiteral( "ACTIVE" ) );
  REQUIRE( sonde.value( QStringLiteral( "frequency" ) ).toDouble() == 32768.0 );
  REQUIRE( sonde.value( QStringLiteral( "mode" ) ).toString() == QStringLiteral( "S" ) );
  REQUIRE( sonde.value( QStringLiteral( "depth_unit" ) ).toString() == QStringLiteral( "cm" ) );
  REQUIRE( sonde.value( QStringLiteral( "depth" ) ).toDouble() == 29.0 );
  REQUIRE( sonde.value( QStringLiteral( "depth_m" ) ).toDouble() == Catch::Approx( 0.29 ) );
  REQUIRE( sonde.value( QStringLiteral( "current_unit" ) ).toString() == QStringLiteral( "mA" ) );
  REQUIRE( sonde.value( QStringLiteral( "current" ) ).toDouble() == 0.0 );

  const QVariantMap line = QfRd8100FrameParser::parseSentence( lineSentence );
  REQUIRE( line.value( QStringLiteral( "mode" ) ).toString() == QStringLiteral( "L" ) );
  REQUIRE( line.value( QStringLiteral( "current" ) ).toDouble() == 33.0 );
  REQUIRE( line.value( QStringLiteral( "frequency" ) ).toDouble() == 65536.0 );

  const QVariantMap inches = QfRd8100FrameParser::parseSentence( inchesSentence );
  REQUIRE( inches.value( QStringLiteral( "depth_unit" ) ).toString() == QStringLiteral( "inches" ) );
  REQUIRE( inches.value( QStringLiteral( "depth_m" ) ).toDouble() == 0.0 );
}

TEST_CASE( "RD8100 invalid sentences" )
{
  QString error;
  QByteArray corrupted = lineSentence;
  corrupted.replace( ",33,", ",34," );
  REQUIRE( QfRd8100FrameParser::parseSentence( corrupted, &error ).isEmpty() );
  REQUIRE( error.startsWith( QStringLiteral( "Invalid checksum" ) ) );

  REQUIRE( QfRd8100FrameParser::parseSentence( QByteArray( "$RDMRX,2,3" ), &error ).isEmpty() );
  REQUIRE( error.startsWith( QStringLiteral( "Missing or malformed checksum" ) ) );

  // Valid NMEA sentence from another talker
  REQUIRE( QfRd8100FrameParser::parseSentence( QByteArray( "$GPGLL,4916.45,N,12311.12,W,225444,A*31" ), &error ).isEmpty() );
  REQUIRE( error.startsWith( QStringLiteral( "Unsupported sentence type" ) ) );
}

TEST_CASE( "RD8100 missing values are NULL" )
{
  QByteArray sentence( "$RDMRX,2,3,F03,0042,ACTIVE,8192Hz,60,---,L,P,furlongs,---,mA,,dB,-60,dBuV,0,*" );
  quint8 checksum = 0;
  for ( qsizetype i = 1; i < sentence.size() - 1; ++i )
  {
    checksum ^= static_cast<quint8>( sentence.at( i ) );
  }
  sentence.append( QByteArray::number( checksum, 16 ).rightJustified( 2, '0' ).toUpper() );

  QString error;
  const QVariantMap measurement = QfRd8100FrameParser::parseSentence( sentence, &error );
  REQUIRE( error.isEmpty() );
  REQUIRE_FALSE( measurement.value( QStringLiteral( "depth" ) ).isValid() );
  REQUIRE_FALSE( measurement.value( QStringLiteral( "depth_m" ) ).isValid() );
  REQUIRE_FALSE( measurement.value( QStringLiteral( "current" ) ).isValid() );
  REQUIRE( measurement.value( QStringLiteral( "log_number" ) ).toInt() == 42 );

  REQUIRE_FALSE( QfRd8100FrameParser::toMeters( 12.0, QStringLiteral( "furlongs" ) ).isValid() );
  REQUIRE( QfRd8100FrameParser::toMeters( 12.0, QStringLiteral( "inches" ) ).toDouble() == Catch::Approx( 0.3048 ) );
  REQUIRE( QfRd8100FrameParser::toMeters( 2.0, QStringLiteral( "ft" ) ).toDouble() == Catch::Approx( 0.6096 ) );
  REQUIRE( QfRd8100FrameParser::toMeters( 1.5, QStringLiteral( "m" ) ).toDouble() == Catch::Approx( 1.5 ) );
}

TEST_CASE( "RD8100 stream reassembly" )
{
  const QByteArray wire = sondeSentence + "\r\n";

  SECTION( "byte by byte" )
  {
    QfRd8100FrameParser parser;
    int measurements = 0;
    for ( char byte : wire )
    {
      const QfRd8100FrameParser::Result result = parser.feed( QByteArray( 1, byte ) );
      REQUIRE( result.errors.isEmpty() );
      measurements += static_cast<int>( result.measurements.size() );
    }
    REQUIRE( measurements == 1 );
    REQUIRE( parser.bufferedBytes() == 0 );
  }

  SECTION( "split as captured" )
  {
    // Packet sizes as observed on the phone (5 bytes, 131 bytes, remainder)
    QfRd8100FrameParser parser;
    REQUIRE( parser.feed( wire.left( 5 ) ).measurements.isEmpty() );
    REQUIRE( parser.feed( wire.mid( 5, 131 ) ).measurements.isEmpty() );
    const QfRd8100FrameParser::Result result = parser.feed( wire.mid( 136 ) );
    REQUIRE( result.measurements.size() == 1 );
    REQUIRE( result.measurements.at( 0 ).value( QStringLiteral( "log_number" ) ).toInt() == 19 );
  }

  SECTION( "several sentences and noise in one packet" )
  {
    QfRd8100FrameParser parser;
    QByteArray corrupted = lineSentence;
    corrupted.replace( ",33,", ",34," );
    const QfRd8100FrameParser::Result result = parser.feed( "\r\n" + wire + "noise" + lineSentence + "\n" + corrupted + "\n" );
    REQUIRE( result.measurements.size() == 2 );
    REQUIRE( result.measurements.at( 1 ).value( QStringLiteral( "current" ) ).toDouble() == 33.0 );
    REQUIRE( result.errors.size() == 1 );
  }

  SECTION( "flush without trailing line feed" )
  {
    QfRd8100FrameParser parser;
    REQUIRE( parser.feed( lineSentence ).measurements.isEmpty() );
    const QfRd8100FrameParser::Result result = parser.flush();
    REQUIRE( result.measurements.size() == 1 );
    REQUIRE( parser.bufferedBytes() == 0 );
  }

  SECTION( "overlong garbage is discarded" )
  {
    QfRd8100FrameParser parser;
    const QfRd8100FrameParser::Result result = parser.feed( QByteArray( QfRd8100FrameParser::MAXIMUM_SENTENCE_LENGTH + 1, 'x' ) );
    REQUIRE( result.errors.size() == 1 );
    REQUIRE( parser.bufferedBytes() == 0 );
    REQUIRE( parser.feed( wire ).measurements.size() == 1 );
  }
}

TEST_CASE( "RD8100 captured corpus" )
{
  QFile file( QStringLiteral( "%1/rd8100/rd8100_pxlm_fw1003_ascii2.txt" ).arg( TEST_DATA_DIR ) );
  REQUIRE( file.open( QIODevice::ReadOnly ) );

  QfRd8100FrameParser parser;
  const QfRd8100FrameParser::Result result = parser.feed( file.readAll() );
  REQUIRE( result.errors.isEmpty() );
  REQUIRE( result.measurements.size() == 19 );
  for ( const QVariantMap &measurement : result.measurements )
  {
    REQUIRE( measurement.value( QStringLiteral( "field_count" ) ).toInt() == 46 );
    REQUIRE( measurement.value( QStringLiteral( "frequency" ) ).isValid() );
  }
}
