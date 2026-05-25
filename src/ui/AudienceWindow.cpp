#include "AudienceWindow.hpp"

#include <QElapsedTimer>
#include <QCursor>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QtMath>
#include <QtGlobal>

#include <utility>

Q_LOGGING_CATEGORY(logUi, "ui")

namespace {
constexpr int maxAudienceTextures = 4;

constexpr char vertexShaderSource[] = R"(
attribute vec2 position;
attribute vec2 texCoord;
varying vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr char fragmentShaderSource[] = R"(
uniform sampler2D slideTexture;
varying vec2 vTexCoord;

void main() {
    gl_FragColor = texture2D(slideTexture, vTexCoord);
}
)";

QImage verticallyFlipped(QImage image) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return std::move(image).flipped(Qt::Vertical);
#else
    return std::move(image).mirrored(false, true);
#endif
}
}

AudienceWindow::AudienceWindow()
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate) {
    setTitle(QStringLiteral("uil Audience"));
    resize(960, 540);
    m_cursorHideTimer.setSingleShot(true);
    m_cursorHideTimer.setInterval(2000);
    connect(&m_cursorHideTimer, &QTimer::timeout, this, &AudienceWindow::hideCursor);
}

AudienceWindow::~AudienceWindow() {
    if (context()) {
        makeCurrent();
        releaseOpenGLResources();
        doneCurrent();
    }
}

void AudienceWindow::setSlideImage(const QString& textureKey, const QImage& image) {
    if (textureKey.isEmpty() || image.isNull()) {
        clearSlideImage();
        return;
    }

    m_currentTextureKey = textureKey;
    cacheSlideImage(textureKey, image);
    update();
}

void AudienceWindow::clearSlideImage() {
    m_currentTextureKey.clear();
    update();
}

void AudienceWindow::cacheSlideImage(const QString& textureKey, const QImage& image) {
    if (textureKey.isEmpty() || image.isNull() || hasTexture(textureKey)) {
        return;
    }

    for (PendingTextureUpload& upload : m_pendingUploads) {
        if (upload.key == textureKey) {
            upload.image = image;
            update();
            return;
        }
    }

    m_pendingUploads.push_back(PendingTextureUpload{textureKey, image});
    update();
}

void AudienceWindow::setVideoFrame(const QImage& image, QRectF slideRect) {
    if (image.isNull() || !slideRect.isValid()) {
        clearVideoOverlay();
        return;
    }

    m_pendingVideoFrame = image;
    m_videoRect = slideRect;
    m_videoFrameDirty = true;
    m_hasVideoOverlay = true;
    update();
}

void AudienceWindow::clearVideoOverlay() {
    m_pendingVideoFrame = {};
    m_videoRect = {};
    m_videoTexture.reset();
    m_videoTextureSize = {};
    m_videoFrameDirty = false;
    m_hasVideoOverlay = false;
    update();
}

void AudienceWindow::setAudienceScreen(QScreen* screen) {
    if (!screen) {
        return;
    }

    m_screen = screen;
    applyScreenGeometry(m_isFullscreen);
    if (m_isFullscreen) {
        showFullScreen();
    }
    emit renderTargetChanged();
}

void AudienceWindow::enterFullscreen() {
    applyScreenGeometry(true);
    showFullScreen();
    m_isFullscreen = true;
    showCursorTemporarily();
    qCInfo(logUi) << "Audience fullscreen entered";
}

void AudienceWindow::toggleFullscreen() {
    if (!m_isFullscreen) {
        enterFullscreen();
    } else {
        exitFullscreen();
    }
}

void AudienceWindow::exitFullscreen() {
    if (!m_isFullscreen) {
        return;
    }

    showNormal();
    hide();
    m_isFullscreen = false;
    clearBlankScreen();
    unsetCursor();
    qCInfo(logUi) << "Audience fullscreen closed";
}

void AudienceWindow::toggleBlackScreen() {
    m_blankMode = (m_blankMode == BlankMode::Black) ? BlankMode::None : BlankMode::Black;
    update();
}

void AudienceWindow::toggleWhiteScreen() {
    m_blankMode = (m_blankMode == BlankMode::White) ? BlankMode::None : BlankMode::White;
    update();
}

void AudienceWindow::clearBlankScreen() {
    if (m_blankMode == BlankMode::None) {
        return;
    }
    m_blankMode = BlankMode::None;
    update();
}

QSize AudienceWindow::renderLogicalSize() const {
    if (m_screen) {
        return m_screen->geometry().size();
    }

    return size();
}

qreal AudienceWindow::renderDevicePixelRatio() const {
    if (m_screen) {
        return m_screen->devicePixelRatio();
    }

    return devicePixelRatio();
}

void AudienceWindow::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)
        || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)
        || !m_program.link()) {
        qCWarning(logUi) << "Audience OpenGL shader setup failed:" << m_program.log();
        return;
    }

    m_vertexArray.create();
    QOpenGLVertexArrayObject::Binder vertexArrayBinder(&m_vertexArray);

    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(4 * int(sizeof(Vertex)));

    m_program.bind();
    const int positionLocation = m_program.attributeLocation("position");
    const int texCoordLocation = m_program.attributeLocation("texCoord");
    m_program.enableAttributeArray(positionLocation);
    m_program.setAttributeBuffer(positionLocation, GL_FLOAT, offsetof(Vertex, x), 2, sizeof(Vertex));
    m_program.enableAttributeArray(texCoordLocation);
    m_program.setAttributeBuffer(texCoordLocation, GL_FLOAT, offsetof(Vertex, u), 2, sizeof(Vertex));
    m_program.setUniformValue("slideTexture", 0);
    m_program.release();
    m_vertexBuffer.release();
    m_openGLReady = true;
}

void AudienceWindow::resizeGL(int width, int height) {
    CachedTexture* texture = currentTexture();
    if (texture) {
        updateVertexBuffer(QSize(width, height), texture->size, QRectF(0.0, 0.0, 1.0, 1.0));
    }
}

void AudienceWindow::paintGL() {
    if (!m_openGLReady || !context() || QOpenGLContext::currentContext() != context()) {
        return;
    }

    const qreal dpr = devicePixelRatio();
    const QSize viewportSize(qMax(1, int(qRound(width() * dpr))),
                             qMax(1, int(qRound(height() * dpr))));
    glViewport(0, 0, viewportSize.width(), viewportSize.height());

    if (m_blankMode == BlankMode::White) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    if (m_blankMode == BlankMode::Black) {
        return;
    }

    uploadPendingTextures();
    uploadPendingVideoTexture();

    CachedTexture* texture = currentTexture();
    if (!texture || !texture->texture || !m_program.isLinked()) {
        return;
    }

    m_program.bind();
    QOpenGLVertexArrayObject::Binder vertexArrayBinder(&m_vertexArray);
    glActiveTexture(GL_TEXTURE0);
    updateVertexBuffer(viewportSize, texture->size, QRectF(0.0, 0.0, 1.0, 1.0));
    texture->texture->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    texture->texture->release();

    if (m_hasVideoOverlay && m_videoTexture && m_videoTextureSize.isValid() && m_videoRect.isValid()) {
        updateVertexBuffer(viewportSize, texture->size, m_videoRect);
        m_videoTexture->bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_videoTexture->release();
    }

    m_program.release();
}

void AudienceWindow::keyPressEvent(QKeyEvent* event) {
    showCursorTemporarily();
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        emit nextRequested();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit previousRequested();
        event->accept();
        return;
    case Qt::Key_Home:
        emit firstRequested();
        event->accept();
        return;
    case Qt::Key_End:
        emit lastRequested();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit playPauseRequested();
        event->accept();
        return;
    case Qt::Key_B:
        toggleBlackScreen();
        event->accept();
        return;
    case Qt::Key_W:
        toggleWhiteScreen();
        event->accept();
        return;
    case Qt::Key_F11:
        toggleFullscreen();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (m_isFullscreen) {
            exitFullscreen();
            event->accept();
            return;
        }
        if (m_blankMode != BlankMode::None) {
            clearBlankScreen();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    QOpenGLWindow::keyPressEvent(event);
}

void AudienceWindow::mouseMoveEvent(QMouseEvent* event) {
    showCursorTemporarily();
    QOpenGLWindow::mouseMoveEvent(event);
}

void AudienceWindow::uploadPendingTextures() {
    if (m_pendingUploads.isEmpty()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();

    int uploaded = 0;
    while (!m_pendingUploads.isEmpty()) {
        const PendingTextureUpload upload = m_pendingUploads.takeFirst();
        if (hasTexture(upload.key) || upload.image.isNull()) {
            continue;
        }

        const QImage textureImage = verticallyFlipped(upload.image.convertToFormat(QImage::Format_RGBA8888));

        auto texture = std::make_unique<QOpenGLTexture>(textureImage);
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);

        m_textureCache.insert(m_textureCache.begin(), CachedTexture{upload.key, textureImage.size(), std::move(texture)});
        ++uploaded;
    }

    evictOldTextures();

    if (uploaded > 0) {
        qCInfo(logUi) << "Uploaded" << uploaded << "audience texture(s) in" << timer.elapsed() << "ms";
    }
}

void AudienceWindow::uploadPendingVideoTexture() {
    if (!m_videoFrameDirty) {
        return;
    }

    m_videoFrameDirty = false;
    m_videoTexture.reset();
    m_videoTextureSize = {};

    if (m_pendingVideoFrame.isNull()) {
        return;
    }

    const QImage textureImage = verticallyFlipped(m_pendingVideoFrame.convertToFormat(QImage::Format_RGBA8888));
    auto texture = std::make_unique<QOpenGLTexture>(textureImage);
    texture->setMinificationFilter(QOpenGLTexture::Linear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_videoTextureSize = textureImage.size();
    m_videoTexture = std::move(texture);
}

void AudienceWindow::updateVertexBuffer(QSize viewportSize, QSize textureSize, QRectF slideRect) {
    if (!m_vertexBuffer.isCreated() || !viewportSize.isValid() || !textureSize.isValid() || !slideRect.isValid()) {
        return;
    }

    const float viewportAspect = float(viewportSize.width()) / float(viewportSize.height());
    const float textureAspect = float(textureSize.width()) / float(textureSize.height());

    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
    if (textureAspect > viewportAspect) {
        halfHeight = viewportAspect / textureAspect;
    } else {
        halfWidth = textureAspect / viewportAspect;
    }

    const float left = -halfWidth + 2.0f * halfWidth * float(slideRect.left());
    const float right = -halfWidth + 2.0f * halfWidth * float(slideRect.right());
    const float top = halfHeight - 2.0f * halfHeight * float(slideRect.top());
    const float bottom = halfHeight - 2.0f * halfHeight * float(slideRect.bottom());

    const Vertex vertices[] = {
        {left,  bottom, 0.0f, 0.0f},
        {right, bottom, 1.0f, 0.0f},
        {left,  top,    0.0f, 1.0f},
        {right, top,    1.0f, 1.0f},
    };

    m_vertexBuffer.bind();
    m_vertexBuffer.write(0, vertices, int(sizeof(vertices)));
    m_vertexBuffer.release();
}

AudienceWindow::CachedTexture* AudienceWindow::currentTexture() {
    if (m_currentTextureKey.isEmpty()) {
        return nullptr;
    }

    for (CachedTexture& texture : m_textureCache) {
        if (texture.key == m_currentTextureKey) {
            return &texture;
        }
    }

    return nullptr;
}

bool AudienceWindow::hasTexture(const QString& textureKey) const {
    for (const CachedTexture& texture : m_textureCache) {
        if (texture.key == textureKey) {
            return true;
        }
    }

    return false;
}

void AudienceWindow::evictOldTextures() {
    for (int i = int(m_textureCache.size()) - 1; int(m_textureCache.size()) > maxAudienceTextures && i >= 0; --i) {
        if (m_textureCache.at(size_t(i)).key == m_currentTextureKey) {
            continue;
        }
        m_textureCache.erase(m_textureCache.begin() + i);
    }
}

void AudienceWindow::applyScreenGeometry(bool fullscreen) {
    if (!m_screen) {
        return;
    }

    setScreen(m_screen);
    if (fullscreen) {
        setGeometry(m_screen->geometry());
        return;
    }

    const QRect availableGeometry = m_screen->availableGeometry();
    QSize targetSize = size();
    if (!targetSize.isValid()) {
        targetSize = QSize(960, 540);
    }
    targetSize = targetSize.boundedTo(availableGeometry.size());

    const QPoint topLeft(
        availableGeometry.x() + (availableGeometry.width() - targetSize.width()) / 2,
        availableGeometry.y() + (availableGeometry.height() - targetSize.height()) / 2);
    setGeometry(QRect(topLeft, targetSize));
}

void AudienceWindow::showCursorTemporarily() {
    unsetCursor();
    if (m_isFullscreen) {
        m_cursorHideTimer.start();
    }
}

void AudienceWindow::hideCursor() {
    if (m_isFullscreen) {
        setCursor(QCursor(Qt::BlankCursor));
    }
}

void AudienceWindow::releaseOpenGLResources() {
    m_openGLReady = false;
    m_textureCache.clear();
    m_pendingUploads.clear();
    m_videoTexture.reset();
    if (m_vertexBuffer.isCreated()) {
        m_vertexBuffer.destroy();
    }
    if (m_vertexArray.isCreated()) {
        m_vertexArray.destroy();
    }
    m_program.removeAllShaders();
}
