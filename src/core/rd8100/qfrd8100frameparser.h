/***************************************************************************
 qfrd8100frameparser.h - QfRd8100FrameParser

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
#ifndef QFRD8100FRAMEPARSER_H
#define QFRD8100FRAMEPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * Parses the ASCII survey measurement sentences sent over Bluetooth by
 * Radiodetection RD8100 locators (BT > PROT > ASCII).
 *
 * A sentence is NMEA-like: "$RDMRX,<fields>,*hh\r\n" where hh is the XOR of
 * all characters between '$' and '*'. Sentences may arrive split across any
 * number of packets, so the parser buffers incoming bytes until a full line
 * is available.
 *
 * Parsed measurements are returned as variant maps whose keys are used, once
 * prefixed with "rd8100_", as expression variables. Every positional field is
 * always exposed (field_0 … field_N) alongside the raw sentence, so that values
 * whose meaning is not yet confirmed remain usable without code changes.
 * \ingroup core
 */
class QfRd8100FrameParser
{
  public:
    //! Result of feeding bytes to the parser
    struct Result
    {
        QList<QVariantMap> measurements;
        QStringList errors;
    };

    //! Maximum length of a sentence, longer garbage is discarded
    static constexpr int MAXIMUM_SENTENCE_LENGTH = 1024;

    /**
     * Appends \a data to the internal buffer and returns all complete sentences
     * that could be extracted, as parsed measurements or as errors.
     */
    Result feed( const QByteArray &data );

    /**
     * Parses whatever remains in the buffer as a final sentence, for devices
     * closing the connection without a trailing line feed, then clears the buffer.
     */
    Result flush();

    //! Clears the internal buffer
    void reset();

    //! Returns the number of buffered bytes not yet parsed
    int bufferedBytes() const { return static_cast<int>( mBuffer.size() ); }

    /**
     * Returns TRUE if \a sentence ("$…*hh", without line ending) carries a valid checksum.
     */
    static bool isChecksumValid( const QByteArray &sentence );

    /**
     * Parses a single \a sentence ("$…*hh", without line ending) into a measurement map.
     * Returns an empty map and sets \a error when the sentence is invalid.
     */
    static QVariantMap parseSentence( const QByteArray &sentence, QString *error = nullptr );

    /**
     * Converts a \a value expressed in \a unit (as sent by the locator, e.g. "cm"
     * or "inches") into meters. Returns an invalid variant when the unit is unknown.
     */
    static QVariant toMeters( double value, const QString &unit );

  private:
    void processLine( QByteArray line, Result &result );

    QByteArray mBuffer;
};

#endif // QFRD8100FRAMEPARSER_H
