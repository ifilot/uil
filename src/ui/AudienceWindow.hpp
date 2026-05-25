#pragma once

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWindow>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class AudienceWindow : public QOpenGLWindow, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit AudienceWindow();
    ~AudienceWindow() override;

    void setSlideImage(const QString& textureKey, const QImage& image);
    void clearSlideImage();
    void cacheSlideImage(const QString& textureKey, const QImage& image);
    void setAudienceScreen(QScreen* screen);
    void enterFullscreen();
    void toggleFullscreen();
    void exitFullscreen();
    QSize renderLogicalSize() const;
    qreal renderDevicePixelRatio() const;

signals:
    void nextRequested();
    void previousRequested();
    void firstRequested();
    void lastRequested();
    void renderTargetChanged();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct CachedTexture {
        QString key;
        QSize size;
        std::unique_ptr<QOpenGLTexture> texture;
    };

    struct PendingTextureUpload {
        QString key;
        QImage image;
    };

    void uploadPendingTextures();
    void updateVertexBuffer(QSize viewportSize);
    CachedTexture* currentTexture();
    bool hasTexture(const QString& textureKey) const;
    void evictOldTextures();
    void applyScreenGeometry(bool fullscreen);
    void releaseOpenGLResources();

    QString m_currentTextureKey;
    std::vector<CachedTexture> m_textureCache;
    QVector<PendingTextureUpload> m_pendingUploads;
    bool m_openGLReady = false;
    QPointer<QScreen> m_screen;
    bool m_isFullscreen = false;

    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vertexArray;
};
