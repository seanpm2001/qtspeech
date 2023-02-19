// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only


#include <QTest>
#include <QTextToSpeech>
#include <QSignalSpy>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#include <qttexttospeech-config.h>

#if QT_CONFIG(speechd)
    #include <libspeechd.h>
    #if LIBSPEECHD_MAJOR_VERSION == 0 && LIBSPEECHD_MINOR_VERSION < 9
        #define HAVE_SPEECHD_BEFORE_090
    #endif
#endif

using namespace Qt::StringLiterals;

enum : int { SpeechDuration = 20000 };

class tst_QTextToSpeech : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase_data();
    void init();

    void capabilities();

    void availableVoices();
    void availableLocales();

    void locale();
    void voice();

    void rate();
    void pitch();
    void volume();

    void sayHello();
    void pauseResume();
    void sayWithVoices();
    void sayWithRates();

    void sayingWord_data();
    void sayingWord();

    void sayingWordWithPause_data();
    void sayingWordWithPause();

    void synthesize_data();
    void synthesize();

    void synthesizeCallback_data();
    void synthesizeCallback();

private:
    static bool hasDefaultAudioOutput()
    {
        return !QMediaDevices::defaultAudioOutput().isNull();
    }
    static void selectWorkingVoice(QTextToSpeech *tts)
    {
        if (tts->engine() == "speechd") {
            // The voices from the "mary-generic" modules are broken in
            // normal installations. Default to one that works.
            if (tts->voice().name().startsWith("dfki-")) {
                for (const auto &voice : tts->availableVoices()) {
                    if (!voice.name().startsWith("dfki-")) {
                        qWarning() << "Replacing default voice" << tts->voice() << "with" << voice;
                        tts->setVoice(voice);
                        break;
                    }
                }
            }
        }
    }

    void onError(QTextToSpeech::ErrorReason error, const QString &errorString) {
        errorReason = error;
        qCritical() << "Error:" << errorString;
    }

    QTextToSpeech::ErrorReason errorReason = QTextToSpeech::ErrorReason::NoError;
};

void tst_QTextToSpeech::initTestCase_data()
{
    qInfo("Available text-to-speech engines:");
    QTest::addColumn<QString>("engine");
    const auto engines = QTextToSpeech::availableEngines();
    if (!engines.size())
        QSKIP("No speech engines available, skipping test case");
    for (const auto &engine : engines) {

        QString engineName = engine;
#if QT_CONFIG(speechd) && defined(LIBSPEECHD_MAJOR_VERSION) && defined(LIBSPEECHD_MINOR_VERSION)
        if (engine == "speechd") {
            engineName += QString(" (using libspeechd %1.%2)").arg(LIBSPEECHD_MAJOR_VERSION)
                                                              .arg(LIBSPEECHD_MINOR_VERSION);
        }
#ifdef HAVE_SPEECHD_BEFORE_090
        engineName += " - libspeechd too old, skipping";
#endif
#endif
        qInfo().noquote() << "- " << engineName;

#ifdef HAVE_SPEECHD_BEFORE_090
        if (engine == "speechd")
            continue;
#endif
        QTest::addRow("%s", engine.toUtf8().constData()) << engine;
    }
}

void tst_QTextToSpeech::init()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine == "speechd") {
        QTextToSpeech tts(engine);
        if (tts.state() == QTextToSpeech::Error) {
            QSKIP("speechd engine reported an error, "
                  "make sure the speech-dispatcher service is running!");
        }
    } else if (engine == "darwin"
        && QOperatingSystemVersion::current() <= QOperatingSystemVersion::MacOSMojave) {
        QTextToSpeech tts(engine);
        if (!tts.availableLocales().size())
            QSKIP("iOS engine is not functional on macOS <= 10.14");
    }
}

void tst_QTextToSpeech::capabilities()
{
    QFETCH_GLOBAL(QString, engine);

    QTextToSpeech tts(engine);
    QVERIFY(tts.engineCapabilities() & QTextToSpeech::Capability::Speak);

    // verify that the mock engine implements all capabilities
    if (engine == "mock") {
        const QMetaEnum capEnum = QMetaEnum::fromType<QTextToSpeech::Capabilities>();
        QTextToSpeech::Capabilities mockCaps = QTextToSpeech::Capability::None;
        for (int index = 0; index < capEnum.keyCount(); ++index) {
            int value = capEnum.keyToValue(capEnum.key(index));
            mockCaps |= QTextToSpeech::Capability(value);
        }

        QCOMPARE(tts.engineCapabilities(), mockCaps);
    }
}

void tst_QTextToSpeech::availableVoices()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    qInfo("Available voices:");
    const auto availableVoices = tts.availableVoices();
    QVERIFY(availableVoices.size() > 0);
    for (const auto &voice : availableVoices)
        qInfo().noquote() << "-" << voice;
}

void tst_QTextToSpeech::availableLocales()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    const auto availableLocales = tts.availableLocales();
    QVERIFY(availableLocales.size() > 0);
    qInfo("Available locales:");
    for (const auto &locale : availableLocales)
        qInfo().noquote() << "-" << locale;
}

/*
    Testing the locale property, and its dependency on the voice
    property.
*/
void tst_QTextToSpeech::locale()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_VERIFY(tts.state() == QTextToSpeech::Ready);

    const auto availableLocales = tts.availableLocales();
    // every engine must have a working default locale if it's Ready
    QVERIFY(availableLocales.contains(tts.locale()));
    if (availableLocales.size() < 2)
        QSKIP("Engine doesn't support more than one locale");

    tts.setLocale(availableLocales[0]);
    // changing the locale results in a voice that is supported by that locale
    const auto voices0 = tts.availableVoices();
    const QVoice voice0 = tts.voice();
    QVERIFY(voices0.contains(voice0));

    QSignalSpy localeSpy(&tts, &QTextToSpeech::localeChanged);
    QSignalSpy voiceSpy(&tts, &QTextToSpeech::voiceChanged);

    // repeat, watch signal emissions
    tts.setLocale(availableLocales[1]);
    QCOMPARE(localeSpy.size(), 1);

    // a locale is only available if it has voices
    const auto voices1 = tts.availableVoices();
    QVERIFY(voices1.size());
    // If the voice is supported in the new locale, then it shouldn't change,
    // otherwise the voice should change as well.
    if (voices1.contains(voice0))
        QCOMPARE(voiceSpy.size(), 0);
    else
        QCOMPARE(voiceSpy.size(), 1);
}

/*
    Testing the voice property, and its dependency on the locale
    property.

    We cannot test all things on engines that have only a single voice, or no
    locale that supports multiple voices.
*/
void tst_QTextToSpeech::voice()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_VERIFY(tts.state() == QTextToSpeech::Ready);

    const QVoice defaultVoice = tts.voice();
    const QLocale defaultLocale = tts.locale();
    // every engine must have a working default voice if it's Ready
    QVERIFY(defaultVoice != QVoice());

    // find a locale with more than one voice, and a voice from another locale
    QList<QVoice> availableVoices;
    QLocale voicesLocale;
    QVoice otherVoice;
    QLocale otherLocale;
    for (const auto &locale : tts.availableLocales()) {
        tts.setLocale(locale);
        const auto voices = tts.availableVoices();
        if (voices.size() > 1 && availableVoices.isEmpty()) {
            availableVoices = voices;
            voicesLocale = locale;
        }
        if (voices != availableVoices && voices.first() != defaultVoice) {
            otherVoice = voices.first();
            otherLocale = locale;
        }
        // we found everything
        if (availableVoices.size() > 1 && otherVoice != QVoice())
            break;
    }
    // if we found neither, then we cannot test
    if (availableVoices.size() < 2 && otherVoice == QVoice())
        QSKIP("Engine %s supports only a single voice", qPrintable(engine));

    tts.setVoice(defaultVoice);
    QCOMPARE(tts.locale(), defaultLocale);

    int expectedVoiceChanged = 0;
    int expectedLocaleChanged = 0;
    QSignalSpy voiceSpy(&tts, &QTextToSpeech::voiceChanged);
    QSignalSpy localeSpy(&tts, &QTextToSpeech::localeChanged);

    if (otherVoice != QVoice() && otherVoice != defaultVoice) {
        QVERIFY(otherLocale != voicesLocale);
        // at this point, setting to otherVoice only changes things when it's not the default
        ++expectedVoiceChanged;
        ++expectedLocaleChanged;
        tts.setVoice(otherVoice);
        QCOMPARE(voiceSpy.size(), expectedVoiceChanged);
        QCOMPARE(tts.locale(), otherLocale);
        QCOMPARE(localeSpy.size(), expectedLocaleChanged);
    } else {
        otherLocale = defaultLocale;
    }

    // two voices from the same locale
    if (availableVoices.size() > 1) {
        if (tts.voice() != availableVoices[0])
            ++expectedVoiceChanged;
        tts.setVoice(availableVoices[0]);
        QCOMPARE(voiceSpy.size(), expectedVoiceChanged);
        if (voicesLocale != otherLocale)
            ++expectedLocaleChanged;
        QCOMPARE(localeSpy.size(), expectedLocaleChanged);
        tts.setVoice(availableVoices[1]);
        QCOMPARE(voiceSpy.size(), ++expectedVoiceChanged);
        QCOMPARE(localeSpy.size(), expectedLocaleChanged);
    }
}

void tst_QTextToSpeech::rate()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);

    tts.setRate(0.5);
    QCOMPARE(tts.rate(), 0.5);
    QSignalSpy spy(&tts, &QTextToSpeech::rateChanged);
    tts.setRate(0.0);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.value(0).first().toDouble(), 0.0);

    tts.setRate(tts.rate());
    QCOMPARE(spy.size(), 1);
}

void tst_QTextToSpeech::pitch()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    tts.setPitch(0.0);
    QCOMPARE(tts.pitch(), 0.0);

    QSignalSpy spy(&tts, &QTextToSpeech::pitchChanged);
    int signalCount = 0;
    for (int i = -10; i <= 10; ++i) {
        tts.setPitch(i / 10.0);
        QString actual = QString("%1").arg(tts.pitch(), 0, 'g', 2);
        QString expected = QString("%1").arg(i / 10.0, 0, 'g', 2);
        QCOMPARE(actual, expected);
        QCOMPARE(spy.size(), ++signalCount);
        tts.setPitch(tts.pitch());
        QCOMPARE(spy.size(), signalCount);
    }
}

void tst_QTextToSpeech::volume()
{
    QFETCH_GLOBAL(QString, engine);
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    tts.setVolume(0.0);

    QSignalSpy spy(&tts, &QTextToSpeech::volumeChanged);
    tts.setVolume(0.7);
    QTRY_COMPARE(spy.size(), 1);
    QVERIFY(spy.value(0).first().toDouble() > 0.6);

    QVERIFY2(tts.volume() > 0.65, QByteArray::number(tts.volume()));
    QVERIFY2(tts.volume() < 0.75, QByteArray::number(tts.volume()));

    tts.setVolume(tts.volume());
    QCOMPARE(spy.size(), 1);
}


void tst_QTextToSpeech::sayHello()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");

    const QString text = QStringLiteral("saying hello with %1");
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);
    auto logger = qScopeGuard([&tts]{
        qWarning() << "Failure with voice" << tts.voice();
    });

    QElapsedTimer timer;
    timer.start();
    tts.say(text.arg(engine));
    QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
    QSignalSpy spy(&tts, &QTextToSpeech::stateChanged);
    QVERIFY(spy.wait(SpeechDuration));
    QCOMPARE(tts.state(), QTextToSpeech::Ready);
    QVERIFY(timer.elapsed() > 100);
    logger.dismiss();
}

void tst_QTextToSpeech::pauseResume()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");
    if (engine == "macos" || engine == "speechd")
        QSKIP("Native speech engine is faulty");

    const QString text = QStringLiteral("Hello. World.");
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);

    tts.say(text);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
    // tts engines will either pause immediately, or in a suitable break,
    // which would be after "Hello."
    tts.pause();
    QTRY_COMPARE(tts.state(), QTextToSpeech::Paused);
    tts.resume();
    QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
}

void tst_QTextToSpeech::sayWithVoices()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");

    const QString text = QStringLiteral("engine %1 with voice of %2");
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);

    const QList<QVoice> voices = tts.availableVoices();
    for (qsizetype i = 0; i < voices.size(); ++i) {

        if (i > 9) {
            qWarning() << "sayWithVoices ended after 10 out of" << voices.size() << "voices.";
            break;
        }

        const auto voice = voices.at(i);
        if (engine == "speechd" && voice.name().startsWith("dfki-")) {
            qWarning() << "Voice dysfunctional:" << voice;
            continue;
        }
        tts.setVoice(voice);
        auto logger = qScopeGuard([&voice]{
            qWarning() << "Failure with voice" << voice;
        });
        QCOMPARE(tts.state(), QTextToSpeech::Ready);

        QElapsedTimer timer;
        timer.start();
        qDebug() << text.arg(engine, voice.name());
        tts.say(text.arg(engine, voice.name()));
        QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
        QSignalSpy spy(&tts, &QTextToSpeech::stateChanged);
        QVERIFY(spy.wait(SpeechDuration));
        QCOMPARE(tts.state(), QTextToSpeech::Ready);
        QVERIFY(timer.elapsed() > 100);
        logger.dismiss();
    }
}

void tst_QTextToSpeech::sayWithRates()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");

    const QString text = QStringLiteral("test at different rates");
    QTextToSpeech tts(engine);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);
    auto logger = qScopeGuard([&tts]{
        qWarning() << "Failure with voice" << tts.voice();
    });

    // warmup at normal rate so that the result doesn't get skewed on engines
    // that initialize lazily on first utterance (like Android).
    tts.say(text);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);

    QElapsedTimer timer;
    qint64 time = 0;
    connect(&tts, &QTextToSpeech::stateChanged, this, [&](QTextToSpeech::State state){
        if (state == QTextToSpeech::Speaking)
            timer.start();
        else if (state == QTextToSpeech::Ready)
            time = timer.elapsed();
    });

    qint64 lastTime = 0;
    // check that speaking at slower rate takes more time (for 0.5, 0.0, -0.5)
    for (int i = 1; i >= -1; --i) {
        time = 0;
        tts.setRate(i * 0.5);
        tts.say(text);
        QTRY_VERIFY2(time > lastTime, qPrintable(QString("%1 took %2, last was %3"_L1)
                                                 .arg(i).arg(time).arg(lastTime)));
        lastTime = time;
    }
    logger.dismiss();
}

void tst_QTextToSpeech::sayingWord_data()
{
    QTest::addColumn<QString>("text");

    QTest::addRow("one word") << "supercalifragilisticexpialidocious";
    QTest::addRow("sentence") << "this is one word.";
    QTest::addRow("punctuation") << "this, if you want: a word!";
    QTest::addRow("two sentences") << "First word. Second word.";
}

void tst_QTextToSpeech::sayingWord()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");

    QFETCH(QString, text);

    const QStringList expectedWords = text.split(QRegularExpression("\\W"), Qt::SkipEmptyParts);

    QTextToSpeech tts(engine);
    if (!(tts.engineCapabilities() & QTextToSpeech::Capability::WordByWordProgress))
        QSKIP("This engine doesn't support word-by-word progress");

    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);

    QElapsedTimer timer;
    QStringList words;
    QList<qint64> times;
    connect(&tts, &QTextToSpeech::sayingWord, [&words, &times, &timer, text](qsizetype start, qsizetype length) {
        words << text.sliced(start, length);
        times << timer.elapsed();
    });

    timer.start();
    tts.say(text);
    auto debugHelper = qScopeGuard([&]{
        qWarning() << "Recorded words:" << words;
        qWarning() << "Expected words:" << expectedWords;
    });
    QTRY_COMPARE(tts.state(), QTextToSpeech::Speaking);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    qint64 totalTime = timer.elapsed();

    QCOMPARE(words, expectedWords);

    // Makes sure that the last word is reported late. Engines need to warm up,
    // and some test data has a "slow" word at the end, but empirically,
    // 40% into the total time is reliable and still makes sure that the signal
    // doesn't get emitted with all words immediately.
    if (words.count() > 1)
        QCOMPARE_GE(times.last(), totalTime * 0.4);

    debugHelper.dismiss();
}

void tst_QTextToSpeech::sayingWordWithPause_data()
{
    QTest::addColumn<QStringList>("words");
    QTest::addColumn<int>("pauseAt");

    const QStringList words{"this", "is", "a", "sentence", "with", "words"};
    QTest::addRow("pause1") << words << 1;
    QTest::addRow("pause4") << words << 4;
}

void tst_QTextToSpeech::sayingWordWithPause()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");
    if (engine == "macos")
        QSKIP("macos engine's pause support is faulty");

    QFETCH(QStringList, words);
    QFETCH(int, pauseAt);

    const QString text = words.join(u' ');

    QTextToSpeech tts(engine);

    if (!(tts.engineCapabilities() & QTextToSpeech::Capability::WordByWordProgress))
        QSKIP("This engine doesn't support word-by-word progress");

    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);

    QStringList spokenWords;
    connect(&tts, &QTextToSpeech::sayingWord, [&](qsizetype start, qsizetype length) {
        spokenWords << text.sliced(start, length);
        if (spokenWords.size() == pauseAt)
            tts.pause(QTextToSpeech::BoundaryHint::Word);
    });

    auto debugHelper = qScopeGuard([&]{
        qWarning() << "Spoken words:" << spokenWords;
    });

    tts.say(text);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Paused);

    // the engine might still signal us about the next word
    QCOMPARE_LE(spokenWords.size(), words.size());
    // wait and verify that no more words are reported
    QTest::qWait(500);
    QCOMPARE_LE(spokenWords.size(), pauseAt + 1);

    // Resume, and make sure that all words are reported.
    // We might get some words reported twice, depending on how
    // the engine supports word bounaries when pausing, and how
    // much of the text it repeats when resuming.
    tts.resume();
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    QTRY_COMPARE_GE(spokenWords.size(), words.size());
    for (const auto &word : words)
        QVERIFY(spokenWords.contains(word));

    debugHelper.dismiss();
}

void tst_QTextToSpeech::synthesize_data()
{
    QTest::addColumn<QString>("text");

    QTest::addRow("text") << "Let's synthesize some text!";
}

void tst_QTextToSpeech::synthesize()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock" && !hasDefaultAudioOutput())
        QSKIP("No audio device present");

    QFETCH(QString, text);

    QTextToSpeech tts(engine);
    if (!(tts.engineCapabilities() & QTextToSpeech::Capability::Synthesize))
        QSKIP("This engine doesn't support synthesize()");

    connect(&tts, &QTextToSpeech::errorOccurred, this, &tst_QTextToSpeech::onError);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    selectWorkingVoice(&tts);

    QElapsedTimer speechTimer;
    // We can't assume that synthesis isn't done before we can check, and that we only
    // have a single change during an event loop cycle, so connect to the signal
    // and keep track ourselves.
    bool running = false;
    bool finished = false;
    qint64 speechTime = 0;
    connect(&tts, &QTextToSpeech::stateChanged, [&running, &finished, &speechTimer, &speechTime](QTextToSpeech::State state) {
        if (state == QTextToSpeech::Synthesizing || state == QTextToSpeech::Speaking) {
            speechTimer.start();
            running = true;
            finished = false;
        }
        if (running && state == QTextToSpeech::Ready) {
            if (!speechTime)
                speechTime = speechTimer.elapsed();
            finished = true;
        }
    });

    // first, measure how long it takes to speak the text
    tts.say(text);
    QTRY_VERIFY(running);
    QTRY_VERIFY(finished);

    running = false;

    QAudioFormat pcmFormat;
    QByteArray pcmData;

    connect(&tts, &QTextToSpeech::synthesized,
            this, [&pcmFormat, &pcmData](const QAudioFormat &format, const QByteArray &bytes) {
        pcmFormat = format;
        pcmData += bytes;
    });

    QElapsedTimer notBlockingTimer;
    notBlockingTimer.start();
    tts.synthesize(text);
    QCOMPARE_LT(notBlockingTimer.elapsed(), 250);
    QTRY_VERIFY(running);
    QTRY_VERIFY(finished);

    QVERIFY(pcmFormat.isValid());
    // bytesForDuration takes micro seconds, we measured in milliseconds.
    const qint32 bytesExpected = pcmFormat.bytesForDuration(speechTime * 1000);

    // We should have as much data as the format requires for the time it took
    // to play the speech, +/- 10% as we can't measure the exact audio duration.
    QCOMPARE_GE(pcmData.size(), double(bytesExpected) * 0.9);
    if (engine == "flite") // flite is very unreliable
        QCOMPARE_LT(pcmData.size(), double(bytesExpected) * 1.5);
    else
        QCOMPARE_LT(pcmData.size(), double(bytesExpected) * 1.1);
}

/*!
    API test for the functor variants of synthesize(), using only the mock
    engine as the engine implementation is identical to the non-functor
    version tested above.
*/
void tst_QTextToSpeech::synthesizeCallback_data()
{
    QTest::addColumn<QString>("text");

    QTest::addRow("one") << "test";
    QTest::addRow("several") << "this will produce more than one chunk.";
}

void tst_QTextToSpeech::synthesizeCallback()
{
    QFETCH_GLOBAL(QString, engine);
    if (engine != "mock")
        QSKIP("Only testing with mock engine");

    QTextToSpeech tts(engine);
    QVERIFY(tts.engineCapabilities() & QTextToSpeech::Capability::Synthesize);

    QFETCH(QString, text);

    QAudioFormat expectedFormat;
    QByteArray expectedBytes;

    // record a reference using the already tested synthesized() signal
    auto connection = connect(&tts, &QTextToSpeech::synthesized,
            [&expectedFormat, &expectedBytes](const QAudioFormat &format, const QByteArray &bytes){
        expectedFormat = format;
        expectedBytes += bytes;
    });
    tts.synthesize(text);
    QTRY_VERIFY(expectedFormat.isValid());
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    tts.disconnect(connection);

    struct Processor : QObject {
        void process(const QAudioFormat &format, const QByteArray &bytes)
        {
            m_format = format;
            m_allBytes += bytes;
        }
        void audioFormatKnown(const QAudioFormat &format)
        {
            m_format = format;
        }
        void reset()
        {
            m_format = {};
            m_allBytes = {};
        }
        QAudioFormat m_format;
        QByteArray m_allBytes;
    } processor;

    // Functor without context
    tts.synthesize(text, [&processor](const QAudioFormat &format, const QByteArray &bytes){
        processor.m_format = format;
        processor.m_allBytes += bytes;
    });
    QTRY_COMPARE(processor.m_format, expectedFormat);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    QCOMPARE(processor.m_allBytes, expectedBytes);
    processor.reset();
    // Functor with context
    tts.synthesize(text, &tts, [&processor](const QAudioFormat &format, const QByteArray &bytes){
        processor.m_format = format;
        processor.m_allBytes += bytes;
    });
    QTRY_COMPARE(processor.m_format, expectedFormat);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    QCOMPARE(processor.m_allBytes, expectedBytes);
    processor.reset();
    // PMF
    tts.synthesize(text, &processor, &Processor::process);
    QTRY_COMPARE(processor.m_format, expectedFormat);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    QCOMPARE(processor.m_allBytes, expectedBytes);
    processor.reset();
    // PMF with no QByteArray argument - not very useful, but Qt allows it
    tts.synthesize(text, &processor, &Processor::audioFormatKnown);
    QTRY_COMPARE(processor.m_format, expectedFormat);
    QTRY_COMPARE(tts.state(), QTextToSpeech::Ready);
    QCOMPARE(processor.m_allBytes, QByteArray());
    processor.reset();
}

QTEST_MAIN(tst_QTextToSpeech)
#include "tst_qtexttospeech.moc"
