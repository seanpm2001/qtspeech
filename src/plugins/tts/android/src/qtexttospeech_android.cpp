/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Speech module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "qtexttospeech_android.h"

#include <QtCore/qcoreapplication.h>

QT_BEGIN_NAMESPACE

static jclass g_qtSpeechClass = 0;

typedef QMap<jlong, QTextToSpeechEngineAndroid *> TextToSpeechMap;
Q_GLOBAL_STATIC(TextToSpeechMap, textToSpeechMap)

static void notifyError(JNIEnv *env, jobject thiz, jlong id)
{
    Q_UNUSED(env);
    Q_UNUSED(thiz);

    QTextToSpeechEngineAndroid *const tts = (*textToSpeechMap)[id];
    if (!tts)
        return;

    QMetaObject::invokeMethod(tts, "processNotifyError", Qt::AutoConnection);
}

static void notifyReady(JNIEnv *env, jobject thiz, jlong id)
{
    Q_UNUSED(env);
    Q_UNUSED(thiz);

    QTextToSpeechEngineAndroid *const tts = (*textToSpeechMap)[id];
    if (!tts)
        return;

    QMetaObject::invokeMethod(tts, "processNotifyReady", Qt::AutoConnection);
}

static void notifySpeaking(JNIEnv *env, jobject thiz, jlong id)
{
    Q_UNUSED(env);
    Q_UNUSED(thiz);

    QTextToSpeechEngineAndroid *const tts = (*textToSpeechMap)[id];
    if (!tts)
        return;

    QMetaObject::invokeMethod(tts, "processNotifySpeaking", Qt::AutoConnection);
}

Q_DECL_EXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void */*reserved*/)
{
    static bool initialized = false;
    if (initialized)
        return JNI_VERSION_1_6;
    initialized = true;

    typedef union {
        JNIEnv *nativeEnvironment;
        void *venv;
    } UnionJNIEnvToVoid;

    UnionJNIEnvToVoid uenv;
    uenv.venv = NULL;

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    JNIEnv *jniEnv = uenv.nativeEnvironment;
    jclass clazz = jniEnv->FindClass("org/qtproject/qt/android/speech/QtTextToSpeech");

    static const JNINativeMethod methods[] = {
        {"notifyError", "(J)V", reinterpret_cast<void *>(notifyError)},
        {"notifyReady", "(J)V", reinterpret_cast<void *>(notifyReady)},
        {"notifySpeaking", "(J)V", reinterpret_cast<void *>(notifySpeaking)}
    };

    if (clazz) {
        g_qtSpeechClass = static_cast<jclass>(jniEnv->NewGlobalRef(clazz));
        if (jniEnv->RegisterNatives(g_qtSpeechClass,
                                    methods,
                                    sizeof(methods) / sizeof(methods[0])) != JNI_OK) {
            return JNI_ERR;
        }
    }

    return JNI_VERSION_1_6;
}

QTextToSpeechEngineAndroid::QTextToSpeechEngineAndroid(const QVariantMap &parameters, QObject *parent)
    : QTextToSpeechEngine(parent)
{
    Q_ASSERT(g_qtSpeechClass);

    const QString engine = parameters.value("androidEngine").toString();

    const jlong id = reinterpret_cast<jlong>(this);
    m_speech = QJniObject::callStaticObjectMethod(
                    g_qtSpeechClass, "open",
                    "(Landroid/content/Context;JLjava/lang/String;)Lorg/qtproject/qt/android/speech/QtTextToSpeech;",
                    QNativeInterface::QAndroidApplication::context(), id, QJniObject::fromString(engine).object());
    (*textToSpeechMap)[id] = this;
}

QTextToSpeechEngineAndroid::~QTextToSpeechEngineAndroid()
{
    textToSpeechMap->remove(reinterpret_cast<jlong>(this));
    m_speech.callMethod<void>("shutdown");
}

void QTextToSpeechEngineAndroid::say(const QString &text)
{
    if (text.isEmpty())
        return;

    if (m_state == QTextToSpeech::Speaking)
        stop();

    m_text = text;
    m_speech.callMethod<void>("say", "(Ljava/lang/String;)V", QJniObject::fromString(m_text).object());
}

QTextToSpeech::State QTextToSpeechEngineAndroid::state() const
{
    return m_state;
}

void QTextToSpeechEngineAndroid::setState(QTextToSpeech::State state)
{
    if (m_state == state)
        return;

    m_state = state;
    emit stateChanged(m_state);
}

void QTextToSpeechEngineAndroid::processNotifyReady()
{
    if (m_state != QTextToSpeech::Paused)
        setState(QTextToSpeech::Ready);
}

void QTextToSpeechEngineAndroid::processNotifyError()
{
    setState(QTextToSpeech::BackendError);
}

void QTextToSpeechEngineAndroid::processNotifySpeaking()
{
    setState(QTextToSpeech::Speaking);
}

void QTextToSpeechEngineAndroid::stop()
{
    if (m_state == QTextToSpeech::Ready)
        return;

    m_speech.callMethod<void>("stop", "()V");
    setState(QTextToSpeech::Ready);
}

void QTextToSpeechEngineAndroid::pause()
{
    if (m_state == QTextToSpeech::Paused)
        return;

    m_speech.callMethod<void>("stop", "()V");
    setState(QTextToSpeech::Paused);
}

void QTextToSpeechEngineAndroid::resume()
{
    if (m_state != QTextToSpeech::Paused)
        return;

    say(m_text);
}

// Android API's pitch is from (0.0, 2.0[ with 1.0 being normal. 0.0 is
// not included, so we have to scale negative Qt pitches from 0.1 to 1.0
double QTextToSpeechEngineAndroid::pitch() const
{
    jfloat pitch = m_speech.callMethod<jfloat>("pitch");
    if (pitch < 1.0f)
        pitch = (pitch - 1.0f) / 0.9f;
    else
        pitch -= 1.0f;
    return double(pitch);
}

bool QTextToSpeechEngineAndroid::setPitch(double pitch)
{
    if (pitch < 0)
        pitch = (pitch * 0.9f) + 1.0f;
    else
        pitch += 1.0f;

    // 0 == SUCCESS
    return m_speech.callMethod<int>("setPitch", "(F)I", pitch) == 0;
}

// Android API's rate is from [0.5, 2.0[, with 1.0 being normal.
double QTextToSpeechEngineAndroid::rate() const
{
    jfloat rate = m_speech.callMethod<jfloat>("rate");
    if (rate < 1.0f)
        rate = (rate - 1.0f) * 2.0f;
    else
        rate -= 1.0f;
    return double(rate);
}

bool QTextToSpeechEngineAndroid::setRate(double rate)
{
    rate = 1.0 + (rate >= 0 ? rate : (rate * 0.5));
    // 0 == SUCCESS
    return m_speech.callMethod<int>("setRate", "(F)I", rate) == 0;
}

double QTextToSpeechEngineAndroid::volume() const
{
    jfloat volume = m_speech.callMethod<jfloat>("volume");
    return volume;
}

bool QTextToSpeechEngineAndroid::setVolume(double volume)
{
    // 0 == SUCCESS
    return m_speech.callMethod<jint>("setVolume", "(F)I", float(volume)) == 0;
}

QList<QLocale> QTextToSpeechEngineAndroid::availableLocales() const
{
    auto locales = m_speech.callObjectMethod("getAvailableLocales", "()Ljava/util/List;");
    int count = locales.callMethod<jint>("size");
    QList<QLocale> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto locale = locales.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
        auto localeLanguage = locale.callObjectMethod<jstring>("getLanguage").toString();
        auto localeCountry = locale.callObjectMethod<jstring>("getCountry").toString();
        if (!localeCountry.isEmpty())
            localeLanguage += QString("_%1").arg(localeCountry).toUpper();
        result << QLocale(localeLanguage);
    }
    return result;
}

bool QTextToSpeechEngineAndroid::setLocale(const QLocale &locale)
{
    QStringList parts = locale.name().split('_');

    if (parts.length() != 2)
        return false;

    QString languageCode = parts.at(0);
    QString countryCode = parts.at(1);

    QJniObject jLocale("java/util/Locale", "(Ljava/lang/String;Ljava/lang/String;)V",
                              QJniObject::fromString(languageCode).object(),
                              QJniObject::fromString(countryCode).object());

    return m_speech.callMethod<jboolean>("setLocale", "(Ljava/util/Locale;)Z", jLocale.object());
}

QLocale QTextToSpeechEngineAndroid::locale() const
{
    auto locale = m_speech.callObjectMethod("getLocale", "()Ljava/util/Locale;");
    if (locale.isValid()) {
        auto localeLanguage = locale.callObjectMethod<jstring>("getLanguage").toString();
        auto localeCountry = locale.callObjectMethod<jstring>("getCountry").toString();
        if (!localeCountry.isEmpty())
            localeLanguage += QString("_%1").arg(localeCountry).toUpper();
        return QLocale(localeLanguage);
    }
    return QLocale();
}

QVoice QTextToSpeechEngineAndroid::javaVoiceObjectToQVoice(QJniObject &obj) const
{
    auto voiceName = obj.callObjectMethod<jstring>("getName").toString();
    QVoice::Gender gender;
    if (voiceName.contains(QStringLiteral("#male"))) {
        gender = QVoice::Male;
    } else if (voiceName.contains(QStringLiteral("#female"))) {
        gender = QVoice::Female;
    } else {
        gender = QVoice::Unknown;
    }
    QJniObject locale = obj.callObjectMethod("getLocale", "()Ljava/util/Locale;");
    QLocale qlocale;
    if (locale.isValid()) {
        auto localeLanguage = locale.callObjectMethod<jstring>("getLanguage").toString();
        auto localeCountry = locale.callObjectMethod<jstring>("getCountry").toString();
        if (!localeCountry.isEmpty())
            localeLanguage += QString("_%1").arg(localeCountry).toUpper();
        qlocale = QLocale(localeLanguage);
    }
    return createVoice(voiceName, qlocale, gender, QVoice::Other, voiceName);
}

QList<QVoice> QTextToSpeechEngineAndroid::availableVoices() const
{
    auto voices = m_speech.callObjectMethod("getAvailableVoices", "()Ljava/util/List;");
    int count = voices.callMethod<jint>("size");
    const QLocale ttsLocale = locale();
    QList<QVoice> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto jvoice = voices.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
        const QVoice voice = javaVoiceObjectToQVoice(jvoice);
        if (voice.locale() == ttsLocale)
            result << voice;
    }
    return result;
}

bool QTextToSpeechEngineAndroid::setVoice(const QVoice &voice)
{
    return m_speech.callMethod<jboolean>("setVoice", "(Ljava/lang/String;)Z",
                                         QJniObject::fromString(voiceData(voice).toString()).object());
}

QVoice QTextToSpeechEngineAndroid::voice() const
{
    auto voice = m_speech.callObjectMethod("getVoice", "()Ljava/lang/Object;");
    if (voice.isValid()) {
        return javaVoiceObjectToQVoice(voice);
    }
    return QVoice();
}

QT_END_NAMESPACE
