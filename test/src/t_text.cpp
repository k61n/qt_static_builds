#include "report.h"
#include "tests.h"

#include <QByteArray>
#include <QCollator>
#include <QCryptographicHash>
#include <QLocale>
#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

void testText()
{
    report::section(QStringLiteral("Strings, Unicode and text codecs"));

    // --- basic string handling ---------------------------------------------
    const QString sample = QString::fromUtf8("Grüße, 世界! \xF0\x9F\x91\x8D");
    report::info(QStringLiteral("sample string"), sample);
    report::info(QStringLiteral("size (UTF-16 code units)"), QString::number(sample.size()));
    report::info(QStringLiteral("size (UTF-8 bytes)"), QString::number(sample.toUtf8().size()));

    report::check(QStringLiteral("UTF-8 encode/decode round trip"),
                  QString::fromUtf8(sample.toUtf8()) == sample);
    report::check(QStringLiteral("QString::arg formatting"),
                  QStringLiteral("%1-%2").arg(QStringLiteral("a")).arg(7)
                          == QStringLiteral("a-7"));
    report::check(QStringLiteral("QString::number/toDouble round trip"),
                  qFuzzyCompare(QString::number(3.14159, 'f', 5).toDouble(), 3.14159));
    report::check(QStringLiteral("split/join"),
                  QStringLiteral("a,b,,c").split(QLatin1Char(',')).join(QLatin1Char('|'))
                          == QStringLiteral("a|b||c"));
    report::check(QStringLiteral("QStringView slicing"),
                  QStringView(sample).mid(0, 5).toString() == QStringLiteral("Grüße"));

    // Unicode tables (case mapping, character categories)
    report::info(QStringLiteral("toUpper(\"straße\")"), QString::fromUtf8("straße").toUpper());
    report::check(QStringLiteral("case conversion of non-ASCII letters"),
                  QString::fromUtf8("grüße").toUpper().contains(QString::fromUtf8("Ü")));
    const QChar han = QChar(0x4E16); // 世
    report::check(QStringLiteral("QChar unicode category tables"),
                  han.isLetter() && han.category() == QChar::Letter_Other);
    report::info(QStringLiteral("Unicode version of Qt tables"),
                 QStringLiteral("script of U+4E16 = %1").arg(int(han.script())));

    // --- string converters (QTextCodec replacement) -------------------------
    {
        QStringEncoder toUtf16(QStringConverter::Utf16LE);
        QStringDecoder fromUtf16(QStringConverter::Utf16LE);
        const QByteArray encoded = toUtf16(sample);
        const QString decoded = fromUtf16(encoded);
        report::check(QStringLiteral("QStringEncoder/QStringDecoder UTF-16LE round trip"),
                      !toUtf16.hasError() && !fromUtf16.hasError() && decoded == sample);
        report::info(QStringLiteral("UTF-16LE encoded size"), QString::number(encoded.size()));
    }
    {
        QStringEncoder toLatin1(QStringConverter::Latin1);
        QStringDecoder fromLatin1(QStringConverter::Latin1);
        const QByteArray encoded = toLatin1(QString::fromUtf8("Grüße"));
        const QString decoded = fromLatin1(encoded);
        report::check(QStringLiteral("Latin-1 codec round trip"),
                      decoded == QString::fromUtf8("Grüße"));
    }
    {
        QStringDecoder utf8(QStringConverter::Utf8);
        const QString decoded = utf8(QByteArray("valid\xC3\xA9\xFF\xFE"));
        Q_UNUSED(decoded)
        report::check(QStringLiteral("invalid UTF-8 input is flagged by the decoder"),
                      utf8.hasError());
    }
    {
        QStringDecoder detected = QStringDecoder(QStringConverter::Utf8);
        report::check(QStringLiteral("QStringDecoder is valid"), detected.isValid());
        report::info(QStringLiteral("system 8-bit codec name"),
                     QString::fromLatin1(QStringConverter::nameForEncoding(
                             QStringConverter::System)));
    }

    // --- regular expressions (PCRE2) ---------------------------------------
    {
        QRegularExpression re(QStringLiteral("(?<key>\\w+)\\s*=\\s*(?<value>\\d+)"));
        report::check(QStringLiteral("QRegularExpression compiles"), re.isValid(),
                      re.errorString());
        const QRegularExpressionMatch m = re.match(QStringLiteral("answer = 42"));
        report::check(QStringLiteral("named capture groups"),
                      m.hasMatch() && m.captured(QStringLiteral("key")) == QStringLiteral("answer")
                              && m.captured(QStringLiteral("value")) == QStringLiteral("42"));

        QRegularExpression uni(QStringLiteral("\\p{Han}+"));
        report::check(QStringLiteral("PCRE2 Unicode property support (\\p{Han})"),
                      uni.isValid() && uni.match(sample).hasMatch(), uni.errorString());

        QRegularExpression ci(QStringLiteral("^grüße"),
                              QRegularExpression::CaseInsensitiveOption
                                      | QRegularExpression::UseUnicodePropertiesOption);
        report::check(QStringLiteral("case-insensitive Unicode matching"),
                      ci.match(QStringLiteral("GRÜSSE, welt")).hasMatch()
                              || ci.match(QString::fromUtf8("GRÜßE, welt")).hasMatch());

        QRegularExpression global(QStringLiteral("\\d+"));
        int count = 0;
        auto it = global.globalMatch(QStringLiteral("1 22 333 4444"));
        while (it.hasNext()) {
            it.next();
            ++count;
        }
        report::check(QStringLiteral("global matching iterator"), count == 4,
                      QStringLiteral("found %1 matches").arg(count));

        QRegularExpression lookbehind(QStringLiteral("(?<=EUR )\\d+"));
        report::check(QStringLiteral("lookbehind assertions"),
                      lookbehind.match(QStringLiteral("price EUR 99")).captured(0)
                              == QStringLiteral("99"));
    }

    // --- text segmentation --------------------------------------------------
    {
        const QString emoji = QString::fromUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
                                                "\xE2\x80\x8D\xF0\x9F\x91\xA7"); // family ZWJ
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, emoji);
        int graphemes = 0;
        while (finder.toNextBoundary() != -1)
            ++graphemes;
        report::info(QStringLiteral("grapheme clusters in ZWJ emoji"), QString::number(graphemes));
        report::soft(QStringLiteral("ZWJ emoji forms a single grapheme cluster"), graphemes == 1,
                     QStringLiteral("got %1").arg(graphemes));

        QTextBoundaryFinder words(QTextBoundaryFinder::Word,
                                  QStringLiteral("one two three four"));
        // Position 0 is already a boundary, toNextBoundary() moves past it.
        int wordStarts = (words.boundaryReasons() & QTextBoundaryFinder::StartOfItem) ? 1 : 0;
        while (words.toNextBoundary() != -1) {
            if (words.boundaryReasons() & QTextBoundaryFinder::StartOfItem)
                ++wordStarts;
        }
        report::check(QStringLiteral("word boundary finder"), wordStarts == 4,
                      QStringLiteral("got %1 word starts").arg(wordStarts));
    }

    // --- collation ----------------------------------------------------------
    {
        QCollator collator(QLocale(QLocale::German, QLocale::Germany));
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        report::info(QStringLiteral("collator locale"), collator.locale().name());
        report::check(QStringLiteral("case-insensitive collation (a < B)"),
                      collator.compare(QStringLiteral("a"), QStringLiteral("B")) < 0);

        QCollator numeric;
        numeric.setNumericMode(true);
        report::check(QStringLiteral("numeric collation (item2 < item10)"),
                      numeric.compare(QStringLiteral("item2"), QStringLiteral("item10")) < 0);

        QStringList names = { QString::fromUtf8("Ärger"), QStringLiteral("Zebra"),
                              QStringLiteral("Apfel") };
        std::sort(names.begin(), names.end(), collator);
        report::info(QStringLiteral("collated sort order"), names.join(QStringLiteral(", ")));
        report::soft(QStringLiteral("locale-aware sort puts umlauts near their base letter"),
                     names.first() != QStringLiteral("Zebra"),
                     names.join(QStringLiteral(", ")));
    }

    // --- byte arrays, encodings, hashes ------------------------------------
    {
        const QByteArray raw = QByteArrayLiteral("Hello, World!");
        report::check(QStringLiteral("Base64 encoding"),
                      raw.toBase64() == QByteArrayLiteral("SGVsbG8sIFdvcmxkIQ=="));
        report::check(QStringLiteral("Base64 decoding"),
                      QByteArray::fromBase64(raw.toBase64()) == raw);
        report::check(QStringLiteral("hex encoding"),
                      QByteArray::fromHex(raw.toHex()) == raw);
        report::check(QStringLiteral("percent encoding"),
                      QByteArray::fromPercentEncoding(
                              QByteArrayLiteral("a b/c").toPercentEncoding())
                              == QByteArrayLiteral("a b/c"));

        const QByteArray sha256 = QCryptographicHash::hash(QByteArrayLiteral("abc"),
                                                           QCryptographicHash::Sha256).toHex();
        report::check(QStringLiteral("SHA-256 known answer test"),
                      sha256 == QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a3"
                                                  "96177a9cb410ff61f20015ad"),
                      QString::fromLatin1(sha256));
        const QByteArray md5 = QCryptographicHash::hash(QByteArrayLiteral("abc"),
                                                        QCryptographicHash::Md5).toHex();
        report::check(QStringLiteral("MD5 known answer test"),
                      md5 == QByteArrayLiteral("900150983cd24fb0d6963f7d28e17f72"),
                      QString::fromLatin1(md5));
        const QByteArray sha3 = QCryptographicHash::hash(QByteArrayLiteral("abc"),
                                                         QCryptographicHash::Sha3_256).toHex();
        report::check(QStringLiteral("SHA3-256 produces a 32 byte digest"), sha3.size() == 64,
                      QString::fromLatin1(sha3));

        const QByteArray hmac = QMessageAuthenticationCode::hash(
                QByteArrayLiteral("Hi There"), QByteArray(20, '\x0b'),
                QCryptographicHash::Sha256).toHex();
        report::check(QStringLiteral("HMAC-SHA256 known answer test (RFC 4231)"),
                      hmac == QByteArrayLiteral("b0344c61d8db38535ca8afceaf0bf12b881dc200"
                                                "c9833da726e9376c2e32cff7"),
                      QString::fromLatin1(hmac));
    }

    // --- zlib compression ---------------------------------------------------
    {
        const QByteArray original = QByteArray("compress me ").repeated(200);
        const QByteArray packed = qCompress(original, 9);
        report::check(QStringLiteral("qCompress/qUncompress round trip"),
                      qUncompress(packed) == original);
        report::check(QStringLiteral("zlib actually compresses"), packed.size() < original.size(),
                      QStringLiteral("%1 -> %2 bytes").arg(original.size()).arg(packed.size()));
        report::info(QStringLiteral("compression ratio"),
                     QStringLiteral("%1 -> %2 bytes").arg(original.size()).arg(packed.size()));
    }

    // --- URLs ---------------------------------------------------------------
    {
        QUrl url(QStringLiteral("https://user@example.org:8443/a/b?x=1&y=two#frag"));
        report::check(QStringLiteral("QUrl parsing"),
                      url.isValid() && url.scheme() == QStringLiteral("https")
                              && url.host() == QStringLiteral("example.org")
                              && url.port() == 8443 && url.path() == QStringLiteral("/a/b")
                              && url.fragment() == QStringLiteral("frag"),
                      url.errorString());
        QUrlQuery query(url);
        report::check(QStringLiteral("QUrlQuery item access"),
                      query.queryItemValue(QStringLiteral("y")) == QStringLiteral("two"));
        report::check(QStringLiteral("QUrl::resolved (relative URLs)"),
                      url.resolved(QUrl(QStringLiteral("../c"))).path()
                              == QStringLiteral("/c"));

        const QUrl idn(QString::fromUtf8("http://bücher.example/"));
        report::info(QStringLiteral("IDN encoded host"),
                     QString::fromLatin1(idn.toEncoded()));
        report::soft(QStringLiteral("IDN/punycode conversion"),
                     idn.toEncoded().contains(QByteArrayLiteral("xn--")),
                     QString::fromLatin1(idn.toEncoded()));
    }
}