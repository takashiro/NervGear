TEMPLATE = lib
TARGET = vrseen
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

CONFIG(debug, debug|release): DEFINES += NDK_DEBUG=1

INCLUDEPATH += \
    jni \
    jni/api \
    jni/core \
    jni/gui \
    jni/io \
    jni/media \
    jni/scene

SOURCES += \
    jni/core/VConstants.cpp \
    jni/core/VAtomicInt.cpp \
    jni/core/VByteArray.cpp \
    jni/core/VChar.cpp \
    jni/core/VEventLoop.cpp \
    jni/core/VJson.cpp \
    jni/core/VLog.cpp \
    jni/core/VLock.cpp \
    jni/core/VModule.cpp \
    jni/core/VPath.cpp \
    jni/core/VSignal.cpp \
    jni/core/VString.cpp \
    jni/core/VThread.cpp \
    jni/core/VTimer.cpp \
    jni/core/VMutex.cpp \
    jni/core/VUserSettings.cpp \
    jni/core/VVariant.cpp \
    jni/core/VWaitCondition.cpp \
    jni/core/android/JniUtils.cpp \
    jni/core/android/VOsBuild.cpp \
    jni/api/VKernel.cpp \
    jni/api/VDevice.cpp \
    jni/api/VDirectRender.cpp \
    jni/api/VEglDriver.cpp \
    jni/api/VLensDistortion.cpp \
    jni/api/VRotationSensor.cpp \
    jni/api/VFrameSmooth.cpp \
    jni/api/VMainActivity.cpp \
    jni/api/VGlShader.cpp \
    jni/api/VGlGeometry.cpp \
    jni/gui/VText.cpp \
    jni/gui/VDialog.cpp \
    jni/gui/VPanel.cpp \
    jni/gui/Fader.cpp \
    jni/gui/CollisionPrimitive.cpp \
    jni/gui/KeyState.cpp \
    jni/io/VBinaryStream.cpp \
    jni/io/VBuffer.cpp \
    jni/io/VDir.cpp \
    jni/io/VFile.cpp \
    jni/io/VIODevice.cpp \
    jni/io/VResource.cpp \
    jni/io/VStandardPath.cpp \
    jni/io/VZipFile.cpp \
    jni/media/VImage.cpp \
    jni/media/VSoundManager.cpp \
    jni/scene/VItem.cpp \
    jni/scene/VScene.cpp \
    jni/scene/BitmapFont.cpp \
    jni/scene/DebugLines.cpp \
    jni/scene/VEyeItem.cpp \
    jni/scene/EyePostRender.cpp \
    jni/scene/GazeCursor.cpp \
    jni/scene/VTexture.cpp \
    jni/scene/ModelView.cpp \
    jni/App.cpp


HEADERS += \
    jni/core/VBasicmath.h \
    jni/core/VConstants.h \
    jni/core/VVector.h \
    jni/core/VMatrix.h \
    jni/core/VGeometry.h \
    jni/core/VTransform.h \
    jni/core/android/GlUtils.h \
    jni/core/android/JniUtils.h \
    jni/core/android/VOsBuild.h \
    jni/core/VAlgorithm.h \
    jni/core/VAtomicInt.h \
    jni/core/VArray.h \
    jni/core/VByteArray.h \
    jni/core/VChar.h \
    jni/core/VCircularQueue.h \
    jni/core/VDeque.h \
    jni/core/VEvent.h \
    jni/core/VEventLoop.h \
    jni/core/VFlags.h \
    jni/core/VJson.h \
    jni/core/VList.h \
    jni/core/VLog.h \
    jni/core/VLock.h \
    jni/core/VLockless.h \
    jni/core/VMap.h \
    jni/core/VModule.h \
    jni/core/VNumber.h \
    jni/core/VPath.h \
    jni/core/VPos.h \
    jni/core/VRect.h \
    jni/core/VSignal.h \
    jni/core/VSize.h \
    jni/core/VString.h \
    jni/core/VStringHash.h \
    jni/core/String_FormatUtil.h \
    jni/core/String_PathUtil.h \
    jni/core/VThread.h \
    jni/core/VTimer.h \
    jni/core/VMutex.h \
    jni/core/List.h \
    jni/core/VVariant.h \
    jni/core/VUserSettings.h \
    jni/core/VWaitCondition.h \
    jni/core/VConstants.h \
    jni/core/VGeometry.h \
    jni/core/VMatrix.h \
    jni/core/VVector.h \
    jni/core/VTransform.h \
    jni/core/VBasicmath.h \
    jni/core/VQuat.h \
    jni/api/VDevice.h \
    jni/api/VDirectRender.h \
    jni/api/VEglDriver.h \
    jni/api/VFrame.h \
    jni/api/VFrameSmooth.h \
    jni/api/VGlGeometry.h \
    jni/api/VGlShader.h \
    jni/api/VInput.h \
    jni/api/VKernel.h \
    jni/api/VLensDistortion.h \
    jni/api/VMainActivity.h \
    jni/api/VRotationSensor.h \
    jni/api/VRotationState.h \
    jni/api/VViewSettings.h \
    jni/gui/VText.h \
    jni/gui/VDialog.h \
    jni/gui/VPanel.h \
    jni/gui/Fader.h \
    jni/gui/CollisionPrimitive.h \
    jni/gui/KeyState.h \
    jni/io/VBinaryStream.h \
    jni/io/VBuffer.h \
    jni/io/VDir.h \
    jni/io/VFile.h \
    jni/io/VIODevice.h \
    jni/io/VResource.h \
    jni/io/VStandardPath.h \
    jni/io/VZipFile.h \
    jni/media/VColor.h \
    jni/media/VImage.h \
    jni/media/VSoundManager.h \
    jni/scene/VItem.h \
    jni/scene/VScene.h \
    jni/scene/BitmapFont.h \
    jni/scene/DebugLines.h \
    jni/scene/VEyeItem.h \
    jni/scene/VTexture.h \
    jni/scene/EyePostRender.h \
    jni/scene/GazeCursor.h \
    jni/scene/GazeCursorLocal.h \
    jni/scene/ModelView.h \
    jni/App.h \
    jni/vglobal.h \
    jni/Input.h

include(jni/3rdparty/minizip/minizip.pri)
include(jni/3rdparty/stb/stb.pri)

# OpenGL ES 3.0
LIBS += -lGLESv3
# GL platform interface
LIBS += -lEGL
# native multimedia
LIBS += -lOpenMAXAL
# logging
LIBS += -llog
# native windows
LIBS += -landroid
# For minizip
LIBS += -lz
# audio
LIBS += -lOpenSLES

linux {
    CONFIG(staticlib) {
        QMAKE_POST_LINK = $$QMAKE_COPY $$system_path($$OUT_PWD/lib$${TARGET}.a) $$system_path($$PWD/libs)
    } else {
        QMAKE_POST_LINK = $$QMAKE_COPY $$system_path($$OUT_PWD/lib$${TARGET}.so) $$system_path($$PWD/libs)
    }
}

include(cflags.pri)
include(deployment.pri)
qtcAddDeployment()
