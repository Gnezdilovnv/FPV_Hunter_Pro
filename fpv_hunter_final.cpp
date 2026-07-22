#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>
#include <QSettings>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QFileDialog>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QListWidgetItem>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QEvent>
#include <QMetaObject>
#include <QApplication>
#include <QScreen>
#include <QRadioButton>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMetaObject>
#include <QTextEdit>
#include <QFormLayout>
#include <QSlider>
#include <QTableWidget>
#include <QHeaderView>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <cmath>
#include <vector>
#include <complex>
#include <random>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <functional>

extern "C" {
#include <iio.h>
}

const QString COLOR_BLACK = "#0a0a0a";
const QString COLOR_DARK_GRAY = "#1a1a1a";
const QString COLOR_GRAY = "#2d2d2d";
const QString COLOR_LIGHT_GRAY = "#888888";
const QString COLOR_WHITE = "#ffffff";
const QString COLOR_ORANGE = "#e67e22";
const QString COLOR_BROWN = "#8B4513";
const QString COLOR_GREEN = "#27ae60";
const QString COLOR_RED = "#e74c3c";
const QString COLOR_GREEN_LIGHT = "#2ecc71";
const QString COLOR_BLUE = "#3498db";
const QString COLOR_YELLOW = "#f1c40f";

struct RecordingSettings {
    QString savePath;
    enum Mode { AUTO, MANUAL } mode = AUTO;
    double startThreshold = -35;
    double stopThreshold = -60;
    int minDuration = 5;
    int maxDuration = 300;
    bool onlyVideo = true;
    bool includeControls = false;
    bool splitByFrequency = true;
    bool saveIQ = true;
    bool saveReports = true;
    bool saveVideo = true;
};

struct ScanSettings {
    double startFreq = 100e6;
    double stopFreq = 6000e6;
    double step = 5e6;
    double bandwidth = 4e6;
    int gain = 40;
    bool agcEnabled = false;
    bool dualMode = false;
    int scanSpeed = 50;
};

struct VideoSettings {
    int resolution = 480;
    int fps = 25;
    QString codec = "X264";
    int bitrate = 2000;
    QString format = "mp4";
    bool autoModulation = true;
    bool autoStandard = true;
    QString modulation = "FM";
    QString standard = "PAL";
};

struct VoiceSettings {
    bool enabled = true;
    bool soundEnabled = true;
    int volume = 80;
    bool notifyVideo = true;
    bool notifyControls = true;
    bool notifyWiFi = false;
};

struct InterceptHistory {
    QString timestamp;
    double frequency;
    QString type;
    double power;
    QString filePath;
};

struct SpectrumMarker {
    double frequency;
    QString label;
    QColor color;
    QString type;
    double power;
    bool isActive;
};

struct SignalInfo {
    double frequency;
    QString type;
    double power;
    double bandwidth;
    QImage lastFrame;
    bool hasVideo;
    bool isActive;
    QString status;
    int windowIndex;
    bool isFullscreen;
    QString modulation;
    QString standard;
    QDateTime firstSeen;
    QDateTime lastSeen;
    int count;
};

static int SignalInfoMetaType = qRegisterMetaType<SignalInfo>("SignalInfo");

class PlutoAdvanced;
class VideoThumbnail;
class FullscreenVideoWindow;
class SpectrumWidget;
class FPVHunterPro;

class VoiceManager {
public:
    VoiceManager() : m_enabled(true) {}
    void say(const QString& text) {
        if (!m_enabled) return;
        qDebug() << "[VOICE]" << text;
    }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
private:
    bool m_enabled;
};

// ====================================================================
// КЛАСС PLUTO (все методы public + исправления)
// ====================================================================

class PlutoAdvanced {
public:
    PlutoAdvanced() : ctx(nullptr), phy(nullptr), rx(nullptr), rx_channel(nullptr), buffer(nullptr), connected(false), m_ip("192.168.2.1") {}
    ~PlutoAdvanced() { disconnect(); }
    
    bool connect(const QString &uri = "ip:192.168.2.1") {
        if (connected) disconnect();
        m_ip = uri;
        QString uriStr = uri;
        if (!uriStr.startsWith("ip:") && !uriStr.startsWith("usb:")) {
            uriStr = "ip:" + uriStr;
        }
        ctx = iio_create_context_from_uri(uriStr.toUtf8().constData());
        if (!ctx) {
            ctx = iio_create_context_from_uri("usb:");
        }
        if (!ctx) {
            m_lastError = "Не удалось подключиться к Pluto";
            return false;
        }
        phy = iio_context_find_device(ctx, "ad9361-phy");
        rx = iio_context_find_device(ctx, "cf-ad9361-lpc");
        if (!phy || !rx) {
            iio_context_destroy(ctx);
            ctx = nullptr;
            m_lastError = "Устройства AD9361 не найдены";
            return false;
        }
        rx_channel = iio_device_find_channel(rx, "voltage0", false);
        if (!rx_channel) {
            m_lastError = "Канал приёма не найден";
            return false;
        }
        iio_channel_enable(rx_channel);
        m_serial = getSerial();
        m_firmware = getFirmwareVersion();
        m_chipModel = getChipModel();
        m_hardwareModel = getHardwareModel();
        setSampleRate(4e6);
        setGain(40);
        setFrequency(100e6);
        setBandwidth(4e6);
        connected = true;
        m_lastError = "";
        return true;
    }
    
    void disconnect() {
        if (buffer) { 
            iio_buffer_destroy(buffer); 
            buffer = nullptr; 
        }
        if (ctx) { 
            iio_context_destroy(ctx); 
            ctx = nullptr; 
        }
        connected = false;
    }
    
    QString getLastError() const { return m_lastError; }
    QString getSerial() const { return m_serial; }
    QString getFirmwareVersion() const { return m_firmware; }
    QString getChipModel() const { return m_chipModel; }
    QString getHardwareModel() const { return m_hardwareModel; }
    QString getIP() const { return m_ip; }
    bool isConnected() const { return connected; }
    double getSampleRate() const { return sample_rate; }
    double getGain() const { return gain; }
    double getFreq() const { return freq; }
    bool isAGC() const { return agc_enabled; }
    
    bool setFrequency(double freq) {
        if (!connected || !phy) return false;
        this->freq = freq;
        return iio_device_attr_write_double(phy, "RX_LO_FREQ", freq) >= 0;
    }
    
    bool setSampleRate(double rate) {
        if (!connected || !phy) return false;
        if (rate <= 0) return false;
        sample_rate = rate;
        int ret = iio_device_attr_write_double(phy, "RX_SAMPLING_FREQ", rate);
        if (ret >= 0) setBandwidth(rate);
        return ret >= 0;
    }
    
    bool setGain(double gain) {
        if (!connected || !phy) return false;
        this->gain = gain;
        iio_device_attr_write(phy, "RX_GAIN_MODE", "manual");
        return iio_device_attr_write_double(phy, "RX_GAIN", gain) >= 0;
    }
    
    bool setAGC(bool enable) {
        if (!connected || !phy) return false;
        agc_enabled = enable;
        if (enable) {
            iio_device_attr_write(phy, "RX_GAIN_MODE", "slow_attack");
        } else {
            iio_device_attr_write(phy, "RX_GAIN_MODE", "manual");
            iio_device_attr_write_double(phy, "RX_GAIN", gain);
        }
        return true;
    }
    
    bool setBandwidth(double bw) {
        if (!connected || !rx) return false;
        if (bw <= 0) return false;
        return iio_device_attr_write_double(rx, "RX_RF_BANDWIDTH", bw) >= 0;
    }
    
    double getRSSI() {
        if (!connected || !phy) return -100;
        double rssi = 0;
        iio_device_attr_read_double(phy, "RX_RSSI", &rssi);
        return rssi;
    }
    
    std::vector<std::complex<float>> receiveSamples(size_t count = 4096) {
        std::vector<std::complex<float>> result;
        if (!connected || !rx_channel) return result;
        if (buffer) { 
            iio_buffer_destroy(buffer); 
            buffer = nullptr; 
        }
        buffer = iio_device_create_buffer(rx, count, false);
        if (!buffer) return result;
        ssize_t bytes = iio_buffer_refill(buffer);
        if (bytes < 0) {
            iio_buffer_destroy(buffer);
            buffer = nullptr;
            return result;
        }
        size_t sample_count = bytes / sizeof(int16_t) / 2;
        result.reserve(sample_count);
        int16_t *data = (int16_t*)iio_buffer_first(buffer, rx_channel);
        if (!data) {
            iio_buffer_destroy(buffer);
            buffer = nullptr;
            return result;
        }
        for (size_t i = 0; i < sample_count; ++i) {
            float i_val = data[i*2] / 2048.0f;
            float q_val = data[i*2+1] / 2048.0f;
            result.push_back(std::complex<float>(i_val, q_val));
        }
        return result;
    }
    
    bool saveIQ(const std::vector<std::complex<float>>& samples, const QString& filename) {
        std::ofstream file(filename.toStdString(), std::ios::binary);
        if (!file.is_open()) return false;
        for (const auto& s : samples) {
            float i = s.real();
            float q = s.imag();
            file.write((char*)&i, sizeof(float));
            file.write((char*)&q, sizeof(float));
        }
        file.close();
        return true;
    }
    
    QString getSerial() {
        if (!ctx) return "Неизвестно";
        const char *serial = iio_context_get_attr_value(ctx, "serial");
        return serial ? QString(serial) : "Неизвестно";
    }
    
    QString getFirmwareVersion() {
        if (!phy) return "Неизвестно";
        char buf[256] = {0};
        iio_device_attr_read(phy, "fw_version", buf, sizeof(buf));
        return QString(buf).trimmed();
    }
    
    QString getChipModel() {
        if (!phy) return "Неизвестно";
        char buf[256] = {0};
        iio_device_attr_read(phy, "model", buf, sizeof(buf));
        return QString(buf).trimmed();
    }
    
    QString getHardwareModel() {
        if (!ctx) return "Неизвестно";
        const char *hw = iio_context_get_attr_value(ctx, "hw_model");
        return hw ? QString(hw) : "Неизвестно";
    }
    
private:
    struct iio_context *ctx;
    struct iio_device *phy;
    struct iio_device *rx;
    struct iio_channel *rx_channel;
    struct iio_buffer *buffer;
    bool connected;
    double sample_rate = 4e6;
    double gain = 40;
    double freq = 100e6;
    bool agc_enabled = false;
    QString m_ip;
    QString m_serial;
    QString m_firmware;
    QString m_chipModel;
    QString m_hardwareModel;
    QString m_lastError;
};

// ====================================================================
// КЛАССЫ ВИДЖЕТОВ
// ====================================================================

class SpectrumWidget : public QWidget {
public:
    SpectrumWidget(QWidget* parent = nullptr);
    void setData(const std::vector<double>& freqs, const std::vector<double>& powers);
    void addMarker(const SpectrumMarker& marker);
    void clearMarkers();
    void setIndicator(double freq, double power);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    int mapFreqToX(double freq, int width) const;
    int mapPowerToY(double power, int height) const;
    double mapXToFreq(int x, int width) const;
    std::vector<double> m_freqs;
    std::vector<double> m_powers;
    std::vector<SpectrumMarker> m_markers;
    double m_indicatorFreq = 0;
    double m_indicatorPower = 0;
};

SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(180);
    setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
        .arg(COLOR_BLACK).arg(COLOR_GRAY));
    setMouseTracking(true);
}

void SpectrumWidget::setData(const std::vector<double>& freqs, const std::vector<double>& powers) {
    m_freqs = freqs;
    m_powers = powers;
    update();
}

void SpectrumWidget::addMarker(const SpectrumMarker& marker) {
    m_markers.push_back(marker);
    update();
}

void SpectrumWidget::clearMarkers() {
    m_markers.clear();
    update();
}

void SpectrumWidget::setIndicator(double freq, double power) {
    m_indicatorFreq = freq;
    m_indicatorPower = power;
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    int w = width();
    int h = height();
    painter.fillRect(0, 0, w, h, QColor(COLOR_BLACK));
    painter.setPen(QColor(COLOR_GRAY));
    for (int i = 0; i < 10; ++i) { int x = i * w / 10; painter.drawLine(x, 0, x, h); }
    for (int i = 0; i < 5; ++i) { int y = i * h / 5; painter.drawLine(0, y, w, y); }
    if (!m_freqs.empty() && !m_powers.empty()) {
        QPen pen; pen.setColor(QColor(COLOR_ORANGE)); pen.setWidth(2); painter.setPen(pen);
        for (size_t i = 1; i < m_freqs.size(); ++i) {
            int x1 = mapFreqToX(m_freqs[i-1], w);
            int y1 = mapPowerToY(m_powers[i-1], h);
            int x2 = mapFreqToX(m_freqs[i], w);
            int y2 = mapPowerToY(m_powers[i], h);
            painter.drawLine(x1, y1, x2, y2);
        }
    }
    for (const auto& marker : m_markers) {
        int x = mapFreqToX(marker.frequency / 1e6, w);
        int y = mapPowerToY(marker.power, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(marker.color);
        painter.drawRect(x - 2, y - 20, 4, 20);
        QColor flagColor = marker.color;
        flagColor.setAlpha(200);
        painter.setBrush(flagColor);
        painter.drawRoundedRect(x + 2, y - 20, 65, 16, 4, 4);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(7);
        painter.setFont(font);
        QString label = marker.type;
        if (label.length() > 8) label = label.left(8) + "...";
        painter.drawText(x + 6, y - 8, label);
        painter.setPen(QColor(COLOR_WHITE));
        font.setPointSize(6);
        painter.setFont(font);
        painter.drawText(x - 15, y + 18, QString::number(marker.frequency / 1e6, 'f', 1) + " МГц");
    }
    if (m_indicatorFreq > 0) {
        int x = mapFreqToX(m_indicatorFreq / 1e6, w);
        painter.setPen(QPen(QColor(COLOR_GREEN), 2, Qt::DashLine));
        painter.drawLine(x, 0, x, h);
    }
    painter.setPen(QColor(COLOR_LIGHT_GRAY));
    QFont font = painter.font();
    font.setPointSize(7);
    painter.setFont(font);
    painter.drawText(0, h - 2, "100");
    painter.drawText(w/4 - 10, h - 2, "1500");
    painter.drawText(w/2 - 10, h - 2, "3000");
    painter.drawText(3*w/4 - 10, h - 2, "4500");
    painter.drawText(w - 20, h - 2, "6000");
    painter.drawText(w - 30, 15, "МГц");
    painter.drawText(2, 15, "dBFS");
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event) {
    double freq = mapXToFreq(event->pos().x(), width());
    setToolTip(QString("Частота: %1 МГц").arg(freq, 0, 'f', 1));
    QWidget::mouseMoveEvent(event);
}

int SpectrumWidget::mapFreqToX(double freq, int width) const {
    return (freq - 100) / 5900 * width;
}
int SpectrumWidget::mapPowerToY(double power, int height) const {
    return (power + 100) / 90 * height;
}
double SpectrumWidget::mapXToFreq(int x, int width) const {
    return 100 + (double)x / width * 5900;
}

// ====================================================================
// ВИДЕО-ОКНО
// ====================================================================

class VideoThumbnail : public QFrame {
    Q_OBJECT
public:
    VideoThumbnail(const SignalInfo& sig, FPVHunterPro* parent = nullptr);
    void updateFrame(const QImage& frame);
    void updateInfo();
    void setSignal(const SignalInfo& newSignal);
    void onExpand();
    void onInfo();
    void mousePressEvent(QMouseEvent* event) override;
    QPushButton* getExpandBtn() const { return m_expandBtn; }
    QPushButton* getInfoBtn() const { return m_infoBtn; }
private:
    SignalInfo m_signal;
    QLabel* m_videoLabel;
    QLabel* m_infoLabel;
    QPushButton* m_expandBtn;
    QPushButton* m_infoBtn;
    FPVHunterPro* m_mainWindow;
};

VideoThumbnail::VideoThumbnail(const SignalInfo& sig, FPVHunterPro* parent) 
    : QFrame((QWidget*)parent), m_signal(sig), m_mainWindow(parent) {
    setFixedSize(200, 160);
    setStyleSheet(QString("background-color: %1; border: 2px solid %2; border-radius: 8px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_GRAY));
    setCursor(Qt::PointingHandCursor);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);
    m_videoLabel = new QLabel();
    m_videoLabel->setFixedSize(180, 100);
    m_videoLabel->setStyleSheet("background-color: #000000; border-radius: 4px;");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setText("📹");
    QFont font = m_videoLabel->font();
    font.setPointSize(24);
    m_videoLabel->setFont(font);
    layout->addWidget(m_videoLabel);
    m_infoLabel = new QLabel();
    m_infoLabel->setStyleSheet(QString("color: %1; font-size: 9px;").arg(COLOR_LIGHT_GRAY));
    m_infoLabel->setAlignment(Qt::AlignCenter);
    updateInfo();
    layout->addWidget(m_infoLabel);
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_expandBtn = new QPushButton("↔ Развернуть");
    m_expandBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; font-size: 8px; padding: 2px 6px; border-radius: 3px;")
        .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    m_expandBtn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_expandBtn);
    m_infoBtn = new QPushButton("ℹ️");
    m_infoBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; font-size: 8px; padding: 2px 6px; border-radius: 3px;")
        .arg(COLOR_GRAY).arg(COLOR_WHITE));
    m_infoBtn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_infoBtn);
    layout->addLayout(btnLayout);
    setLayout(layout);
    connect(m_expandBtn, &QPushButton::clicked, this, &VideoThumbnail::onExpand);
    connect(m_infoBtn, &QPushButton::clicked, this, &VideoThumbnail::onInfo);
}

void VideoThumbnail::updateFrame(const QImage& frame) {
    if (!frame.isNull()) {
        QPixmap pix = QPixmap::fromImage(frame).scaled(180, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_videoLabel->setPixmap(pix);
    }
}

void VideoThumbnail::updateInfo() {
    QString text = QString("%1 МГц | %2\n%3 | %4 dBFS")
        .arg(m_signal.frequency / 1e6, 0, 'f', 1)
        .arg(m_signal.type)
        .arg(m_signal.modulation.isEmpty() ? "FM" : m_signal.modulation)
        .arg(m_signal.power, 0, 'f', 1);
    m_infoLabel->setText(text);
}

void VideoThumbnail::setSignal(const SignalInfo& newSignal) {
    m_signal = newSignal;
    updateInfo();
    if (m_signal.hasVideo && !m_signal.lastFrame.isNull()) {
        updateFrame(m_signal.lastFrame);
    }
}

void VideoThumbnail::onExpand() {
    if (m_mainWindow) m_mainWindow->onVideoExpanded(m_signal);
}

void VideoThumbnail::onInfo() {
    if (m_mainWindow) m_mainWindow->showSignalInfo(m_signal);
}

void VideoThumbnail::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        onExpand();
    }
    QFrame::mousePressEvent(event);
}

// ====================================================================
// ПОЛНОЭКРАННОЕ ВИДЕО
// ====================================================================

class FullscreenVideoWindow : public QWidget {
    Q_OBJECT
public:
    FullscreenVideoWindow(const SignalInfo& initialSignal, 
                          const std::vector<SignalInfo>& allSignals,
                          FPVHunterPro* parent = nullptr);
    void updateVideoFrame();
    void updateVideo(const QImage& frame);
    void updateInfo();
    void switchToSignal(const SignalInfo& signal);
signals:
    void signalSwitched(const SignalInfo& signal);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    QWidget* createVideoItem(const SignalInfo& signal);
    QLabel* m_videoLabel;
    QLabel* m_infoLabel;
    QLabel* m_recordingIndicator;
    QWidget* m_listContainer;
    SignalInfo m_currentSignal;
    std::vector<SignalInfo> m_allSignals;
    FPVHunterPro* m_mainWindow;
    QTimer* m_updateTimer;
};

FullscreenVideoWindow::FullscreenVideoWindow(const SignalInfo& initialSignal, 
                          const std::vector<SignalInfo>& allSignals,
                          FPVHunterPro* parent)
    : QWidget((QWidget*)parent), m_currentSignal(initialSignal), m_allSignals(allSignals), m_mainWindow(parent) {
    setWindowTitle(QString("FPV Hunter - Видео на %1 МГц").arg(initialSignal.frequency / 1e6, 0, 'f', 1));
    setMinimumSize(900, 650);
    setStyleSheet(QString("background-color: %1;").arg(COLOR_BLACK));
    setWindowFlags(Qt::Window);
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    QWidget* videoWidget = new QWidget();
    videoWidget->setStyleSheet(QString("background-color: #000000; border: 2px solid %1; border-radius: 8px;")
        .arg(COLOR_ORANGE));
    QVBoxLayout* videoLayout = new QVBoxLayout(videoWidget);
    m_videoLabel = new QLabel();
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(640, 480);
    m_videoLabel->setStyleSheet("color: #ffffff; font-size: 18px;");
    m_videoLabel->setText("📹 Ожидание видео...");
    QFont font = m_videoLabel->font();
    font.setPointSize(36);
    m_videoLabel->setFont(font);
    videoLayout->addWidget(m_videoLabel);
    QHBoxLayout* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel();
    m_infoLabel->setStyleSheet(QString("color: %1; font-size: 12px; padding: 4px; background-color: %2; border-radius: 4px;")
        .arg(COLOR_WHITE).arg(COLOR_DARK_GRAY));
    infoLayout->addWidget(m_infoLabel);
    m_recordingIndicator = new QLabel("⏸");
    m_recordingIndicator->setStyleSheet(QString("color: %1; font-size: 14px; padding: 4px;")
        .arg(COLOR_RED));
    infoLayout->addWidget(m_recordingIndicator);
    infoLayout->addStretch();
    videoLayout->addLayout(infoLayout);
    mainLayout->addWidget(videoWidget, 3);
    QWidget* listWidget = new QWidget();
    listWidget->setMaximumWidth(300);
    listWidget->setStyleSheet(QString("background-color: %1; border-left: 2px solid %2;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_GRAY));
    QVBoxLayout* listLayout = new QVBoxLayout(listWidget);
    QLabel* titleLabel = new QLabel("📋 ДРУГИЕ ВИДЕО");
    titleLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 14px; padding: 8px;")
        .arg(COLOR_ORANGE));
    listLayout->addWidget(titleLabel);
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: none; background-color: transparent;");
    m_listContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(m_listContainer);
    containerLayout->setSpacing(6);
    containerLayout->setContentsMargins(4, 4, 4, 4);
    for (const auto& s : allSignals) {
        if (s.hasVideo && std::abs(s.frequency - initialSignal.frequency) > 0.1) {
            QWidget* item = createVideoItem(s);
            containerLayout->addWidget(item);
        }
    }
    containerLayout->addStretch();
    scrollArea->setWidget(m_listContainer);
    listLayout->addWidget(scrollArea);
    QPushButton* closeBtn = new QPushButton("✕ Закрыть");
    closeBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 8px; border-radius: 4px; font-weight: bold;")
        .arg(COLOR_RED).arg(COLOR_WHITE));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &FullscreenVideoWindow::close);
    listLayout->addWidget(closeBtn);
    mainLayout->addWidget(listWidget, 1);
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &FullscreenVideoWindow::updateVideoFrame);
    m_updateTimer->start(50);
}

void FullscreenVideoWindow::updateVideoFrame() {
    QImage frame(640, 480, QImage::Format_RGB888);
    frame.fill(Qt::black);
    QPainter painter(&frame);
    painter.setPen(Qt::white);
    for (int i = 0; i < 640; ++i) {
        int y = 240 + 80 * sin(i * 0.02 + QDateTime::currentMSecsSinceEpoch() / 1000.0);
        painter.drawPoint(i, y);
    }
    painter.setPen(QColor(COLOR_GREEN));
    painter.drawText(20, 40, QString("📡 %1 МГц | %2 | %3 dBFS")
        .arg(m_currentSignal.frequency / 1e6, 0, 'f', 1)
        .arg(m_currentSignal.type)
        .arg(m_currentSignal.power, 0, 'f', 1));
    painter.drawText(20, 65, QString("🔄 %1 | %2")
        .arg(m_currentSignal.modulation.isEmpty() ? "FM" : m_currentSignal.modulation)
        .arg(m_currentSignal.standard.isEmpty() ? "PAL" : m_currentSignal.standard));
    updateVideo(frame);
}

void FullscreenVideoWindow::updateVideo(const QImage& frame) {
    if (!frame.isNull()) {
        QPixmap pix = QPixmap::fromImage(frame).scaled(m_videoLabel->width(), m_videoLabel->height(), 
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_videoLabel->setPixmap(pix);
    }
}

void FullscreenVideoWindow::updateInfo() {
    m_infoLabel->setText(QString("📡 %1 МГц | %2 | %3 dBFS | %4 | %5")
        .arg(m_currentSignal.frequency / 1e6, 0, 'f', 1)
        .arg(m_currentSignal.type)
        .arg(m_currentSignal.power, 0, 'f', 1)
        .arg(m_currentSignal.modulation.isEmpty() ? "FM" : m_currentSignal.modulation)
        .arg(m_currentSignal.standard.isEmpty() ? "PAL" : m_currentSignal.standard));
}

void FullscreenVideoWindow::switchToSignal(const SignalInfo& signal) {
    m_currentSignal = signal;
    updateInfo();
    setWindowTitle(QString("FPV Hunter - Видео на %1 МГц").arg(signal.frequency / 1e6, 0, 'f', 1));
    if (m_mainWindow) {
        m_mainWindow->setCurrentVideoSignal(signal);
    }
    emit signalSwitched(signal);
}

void FullscreenVideoWindow::closeEvent(QCloseEvent* event) {
    m_updateTimer->stop();
    QWidget::closeEvent(event);
}

QWidget* FullscreenVideoWindow::createVideoItem(const SignalInfo& signal) {
    QWidget* item = new QWidget();
    item->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 4px;")
        .arg(COLOR_BLACK).arg(COLOR_GRAY));
    item->setCursor(Qt::PointingHandCursor);
    QHBoxLayout* layout = new QHBoxLayout(item);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);
    QLabel* preview = new QLabel();
    preview->setFixedSize(70, 50);
    preview->setStyleSheet("background-color: #000000; border-radius: 4px;");
    if (!signal.lastFrame.isNull()) {
        QPixmap pix = QPixmap::fromImage(signal.lastFrame).scaled(70, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        preview->setPixmap(pix);
    } else {
        preview->setText("📹");
        preview->setAlignment(Qt::AlignCenter);
        QFont font = preview->font();
        font.setPointSize(14);
        preview->setFont(font);
    }
    layout->addWidget(preview);
    QVBoxLayout* infoLayout = new QVBoxLayout();
    QLabel* freqLabel = new QLabel(QString("%1 МГц").arg(signal.frequency / 1e6, 0, 'f', 1));
    freqLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 10px;").arg(COLOR_WHITE));
    infoLayout->addWidget(freqLabel);
    QLabel* typeLabel = new QLabel(signal.type);
    typeLabel->setStyleSheet(QString("color: %1; font-size: 9px;").arg(COLOR_LIGHT_GRAY));
    infoLayout->addWidget(typeLabel);
    layout->addLayout(infoLayout);
    QPushButton* switchBtn = new QPushButton("▶");
    switchBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; border-radius: 4px; padding: 4px 8px; font-weight: bold;")
        .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    switchBtn->setCursor(Qt::PointingHandCursor);
    switchBtn->setFixedSize(30, 30);
    connect(switchBtn, &QPushButton::clicked, [this, signal]() {
        switchToSignal(signal);
    });
    layout->addWidget(switchBtn);
    item->setLayout(layout);
    return item;
}

// ====================================================================
// ГЛАВНОЕ ОКНО FPVHunterPro
// ====================================================================

class FPVHunterPro : public QMainWindow {
    Q_OBJECT

public:
    FPVHunterPro(QWidget *parent = nullptr);
    ~FPVHunterPro();
    void startAutoScan();
    void addSignal(const SignalInfo& signal);
    void updateSpectrum(double freq, double power);
    void updateStatus(const QString& status);
    void updateUI();
    void checkRecording();
    void onVideoExpanded(const SignalInfo& signal);
    void onVideoClicked(const SignalInfo& signal);
    void chooseSaveFolder();
    void showSettingsDialog();
    void reconnectPluto();
    void setCurrentVideoSignal(const SignalInfo& signal);
    void updatePlutoStatus();
    void addHistory(const SignalInfo& signal);
    void showSignalInfo(const SignalInfo& signal);

private:
    void setupUI();
    void scanLoop();
    QImage generateDemoFrame(double freq);
    void updateSignalList();
    void updateVideoGrid();
    void updateSpectrumMarkers();
    void updateHistoryTable();
    SignalInfo getBestSignal();
    void startRecording(const SignalInfo& signal);
    void stopRecording();
    QString detectModulation(const std::vector<std::complex<float>>& samples);
    QString detectVideoStandard(const std::vector<std::complex<float>>& samples);
    QString detectType(double freq, double bandwidth, const QString& modulation);
    double estimateBandwidth(const std::vector<std::complex<float>>& samples);
    QString getStyle();
    void loadSettings();
    void saveSettings();
    void applyScanSettings();

    PlutoAdvanced* m_pluto;
    VoiceManager* m_voice;
    QTimer* m_timer;
    QTimer* m_recordTimer;
    QTimer* m_plutoStatusTimer;
    QMutex m_mutex;
    
    bool m_isScanning = false;
    bool m_isViewing = false;
    bool m_isRecording = false;
    bool m_autoRecording = false;
    
    QListWidget* m_signalList;
    QGridLayout* m_videoGrid;
    SpectrumWidget* m_spectrumWidget;
    QLabel* m_signalCountLabel;
    QLabel* m_recordingStatusLabel;
    QLabel* m_plutoStatusLabel;
    QLabel* m_indicatorLabel;
    QTableWidget* m_historyTable;
    QPushButton* m_recordBtn;
    
    std::vector<SignalInfo> m_signals;
    SignalInfo m_currentVideoSignal;
    std::vector<std::pair<double, double>> m_spectrumData;
    std::vector<InterceptHistory> m_history;
    
    RecordingSettings m_recordingSettings;
    ScanSettings m_scanSettings;
    VideoSettings m_videoSettings;
    VoiceSettings m_voiceSettings;

    friend class VideoThumbnail;
    friend class FullscreenVideoWindow;
};

// ====================================================================
// РЕАЛИЗАЦИЯ FPVHunterPro
// ====================================================================

FPVHunterPro::FPVHunterPro(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("FPV HUNTER PRO v8.0 - Полная версия");
    setMinimumSize(1300, 750);
    setStyleSheet(getStyle());
    
    m_pluto = new PlutoAdvanced();
    m_voice = new VoiceManager();
    
    m_recordingSettings.savePath = QDir::homePath() + "/Documents/FPV_Captures";
    loadSettings();
    
    reconnectPluto();
    
    if (!m_pluto->isConnected()) {
        QMessageBox::critical(this, "Ошибка", "Pluto+ не найден!\nПроверьте подключение и IP-адрес.\nПрограмма будет закрыта.");
        QApplication::quit();
        return;
    }
    
    setupUI();
    startAutoScan();
    
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &FPVHunterPro::updateUI);
    m_timer->start(100);
    
    m_recordTimer = new QTimer(this);
    connect(m_recordTimer, &QTimer::timeout, this, &FPVHunterPro::checkRecording);
    m_recordTimer->start(1000);
    
    m_plutoStatusTimer = new QTimer(this);
    connect(m_plutoStatusTimer, &QTimer::timeout, this, &FPVHunterPro::updatePlutoStatus);
    m_plutoStatusTimer->start(3000);
    
    srand(time(nullptr));
    
    if (m_voiceSettings.enabled) {
        m_voice->say("FPV Hunter Pro запущен. Сканирование начато.");
    }
}

FPVHunterPro::~FPVHunterPro() {
    saveSettings();
    delete m_pluto;
    delete m_voice;
}

void FPVHunterPro::reconnectPluto() {
    m_pluto->disconnect();
    QString ip = "192.168.2.1";
    if (m_pluto->connect(ip)) {
        statusBar()->showMessage("✅ Pluto+ подключен");
        updatePlutoStatus();
    } else {
        statusBar()->showMessage("⚠️ Pluto не найден");
        m_plutoStatusLabel->setText("🔴 Pluto: не подключен");
    }
}

void FPVHunterPro::updatePlutoStatus() {
    if (m_pluto->isConnected()) {
        QString info = QString("🟢 Pluto: %1 | %2 | %3 | %4")
            .arg(m_pluto->getSerial())
            .arg(m_pluto->getChipModel())
            .arg(m_pluto->getFirmwareVersion())
            .arg(m_pluto->getHardwareModel());
        m_plutoStatusLabel->setText(info);
        m_plutoStatusLabel->setStyleSheet(QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px; font-size: 10px;")
            .arg(COLOR_WHITE).arg(COLOR_DARK_GRAY));
    } else {
        m_plutoStatusLabel->setText("🔴 Pluto: не подключен");
        m_plutoStatusLabel->setStyleSheet(QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px; font-size: 10px;")
            .arg(COLOR_RED).arg(COLOR_DARK_GRAY));
    }
}

void FPVHunterPro::applyScanSettings() {
    if (m_pluto->isConnected()) {
        m_pluto->setGain(m_scanSettings.gain);
        m_pluto->setAGC(m_scanSettings.agcEnabled);
        m_pluto->setSampleRate(m_scanSettings.bandwidth);
        m_pluto->setBandwidth(m_scanSettings.bandwidth);
    }
}

void FPVHunterPro::startAutoScan() {
    m_isScanning = true;
    applyScanSettings();
    statusBar()->showMessage("🔍 Автоматическое сканирование запущено...");
    QThread::create([this]() { scanLoop(); })->start();
}

void FPVHunterPro::scanLoop() {
    double start = m_scanSettings.startFreq;
    double stop = m_scanSettings.stopFreq;
    double step = m_scanSettings.step;
    double bw = m_scanSettings.bandwidth;
    int counter = 0;
    while (m_isScanning) {
        for (double freq = start; freq < stop && m_isScanning; freq += step) {
            if (m_pluto->isConnected()) {
                m_pluto->setFrequency(freq + bw/2);
                m_pluto->setSampleRate(bw);
                m_pluto->setBandwidth(bw);
                QThread::msleep(5);
                auto samples = m_pluto->receiveSamples(512);
                if (!samples.empty()) {
                    double power = 0;
                    for (const auto& s : samples) power += std::norm(s);
                    power = 10 * log10(power / samples.size() + 1e-12);
                    double rssi = m_pluto->getRSSI();
                    QMetaObject::invokeMethod(this, "updateSpectrum", 
                        Q_ARG(double, freq + bw/2), Q_ARG(double, power));
                    QString modulation = detectModulation(samples);
                    QString standard = detectVideoStandard(samples);
                    double bw_est = estimateBandwidth(samples);
                    QString type = detectType(freq + bw/2, bw_est, modulation);
                    if (power > -50 || rssi > -50) {
                        SignalInfo signal;
                        signal.frequency = freq + bw/2;
                        signal.power = power;
                        signal.type = type;
                        signal.hasVideo = type.contains("FPV");
                        signal.isActive = true;
                        signal.bandwidth = bw_est;
                        signal.modulation = modulation;
                        signal.standard = standard;
                        signal.firstSeen = QDateTime::currentDateTime();
                        signal.lastSeen = QDateTime::currentDateTime();
                        signal.count = 1;
                        signal.lastFrame = generateDemoFrame(freq + bw/2);
                        QMetaObject::invokeMethod(this, "addSignal", 
                            Q_ARG(SignalInfo, signal));
                    }
                }
            }
            QThread::msleep(5);
            if (counter++ % 100 == 0) {
                QMetaObject::invokeMethod(this, "updateStatus", 
                    Q_ARG(QString, QString("🔍 %1 МГц").arg(freq / 1e6, 0, 'f', 0)));
            }
        }
        QThread::msleep(50);
    }
}

double FPVHunterPro::estimateBandwidth(const std::vector<std::complex<float>>& samples) {
    if (samples.empty()) return 0;
    int n = samples.size();
    if (n == 0) return 0;
    std::vector<double> power(n);
    double max_power = 0;
    for (int i = 0; i < n; ++i) {
        power[i] = std::norm(samples[i]);
        if (power[i] > max_power) max_power = power[i];
    }
    double threshold = max_power * 0.3;
    int start = 0, end = n - 1;
    for (int i = 0; i < n; ++i) {
        if (power[i] > threshold) { start = i; break; }
    }
    for (int i = n - 1; i >= 0; --i) {
        if (power[i] > threshold) { end = i; break; }
    }
    if (end > start) {
        return (end - start) * m_scanSettings.bandwidth / n;
    }
    return 0;
}

QString FPVHunterPro::detectModulation(const std::vector<std::complex<float>>& samples) {
    if (samples.empty()) return "FM";
    double mean_amp = 0;
    double var_amp = 0;
    for (const auto& s : samples) {
        double amp = std::abs(s);
        mean_amp += amp;
    }
    mean_amp /= samples.size();
    for (const auto& s : samples) {
        double amp = std::abs(s);
        var_amp += (amp - mean_amp) * (amp - mean_amp);
    }
    var_amp /= samples.size();
    double cv = std::sqrt(var_amp) / (mean_amp + 1e-12);
    double bw = estimateBandwidth(samples);
    if (cv < 0.2 && bw > 50e3) return "FM";
    if (cv > 0.3 && bw < 20e3) return "AM";
    return "FM";
}

QString FPVHunterPro::detectVideoStandard(const std::vector<std::complex<float>>& samples) {
    if (samples.empty()) return "PAL";
    int n = samples.size();
    if (n == 0) return "PAL";
    std::vector<double> power(n);
    for (int i = 0; i < n; ++i) power[i] = std::norm(samples[i]);
    double sample_rate = m_scanSettings.bandwidth;
    double max_power = 0;
    int max_idx = 0;
    for (int i = n/4; i < n/2 && i < n; ++i) {
        if (power[i] > max_power) { max_power = power[i]; max_idx = i; }
    }
    double peak_freq = (double)max_idx / n * sample_rate;
    if (peak_freq > 4.0e6 && peak_freq < 4.8e6) return "PAL";
    if (peak_freq > 3.3e6 && peak_freq < 3.9e6) return "NTSC";
    return "PAL";
}

QString FPVHunterPro::detectType(double freq, double bandwidth, const QString& modulation) {
    Q_UNUSED(modulation);
    if (freq >= 900e6 && freq <= 6000e6 && bandwidth > 5e6) {
        return "FPV Analog";
    } else if (freq >= 2400e6 && freq <= 2483e6 && bandwidth < 5e6) {
        return "Пульт DJI";
    } else if (freq >= 900e6 && freq <= 930e6 && bandwidth < 5e6) {
        return "Пульт 900МГц";
    } else if (freq >= 2412e6 && freq <= 2484e6 && bandwidth > 10e6) {
        return "WiFi";
    }
    return "Неизвестный";
}

QImage FPVHunterPro::generateDemoFrame(double freq) {
    QImage img(180, 100, QImage::Format_RGB888);
    img.fill(Qt::black);
    QPainter painter(&img);
    painter.setPen(Qt::white);
    for (int i = 0; i < 180; ++i) {
        int y = 50 + 30 * sin(i * 0.05 + freq / 1e6);
        painter.drawPoint(i, y);
    }
    painter.setPen(QColor(COLOR_GREEN));
    painter.drawText(10, 20, QString("%1 МГц").arg(freq / 1e6, 0, 'f', 1));
    double pwr = -30 - (rand() % 20);
    painter.drawText(10, 35, QString("%1 dBFS").arg(pwr, 0, 'f', 1));
    return img;
}

void FPVHunterPro::addSignal(const SignalInfo& signal) {
    QMutexLocker locker(&m_mutex);
    bool found = false;
    for (auto& existing : m_signals) {
        if (std::abs(existing.frequency - signal.frequency) < 0.1e6) {
            existing = signal;
            existing.count++;
            existing.lastSeen = QDateTime::currentDateTime();
            found = true;
            break;
        }
    }
    if (!found) {
        m_signals.push_back(signal);
        addHistory(signal);
        if (m_voiceSettings.enabled) {
            if (signal.hasVideo && m_voiceSettings.notifyVideo) {
                m_voice->say(QString("Обнаружено видео на %1 мегагерц").arg(signal.frequency / 1e6, 0, 'f', 1));
            } else if (signal.type.contains("Пульт") && m_voiceSettings.notifyControls) {
                m_voice->say(QString("Обнаружен пульт на %1 мегагерц").arg(signal.frequency / 1e6, 0, 'f', 1));
            }
        }
    }
    updateSignalList();
    updateVideoGrid();
    updateSpectrumMarkers();
    updateHistoryTable();
    if (signal.hasVideo && m_recordingSettings.mode == RecordingSettings::AUTO) {
        startRecording(signal);
    }
}

void FPVHunterPro::addHistory(const SignalInfo& signal) {
    InterceptHistory record;
    record.timestamp = QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss");
    record.frequency = signal.frequency;
    record.type = signal.type;
    record.power = signal.power;
    record.filePath = "";
    m_history.push_back(record);
    if (m_history.size() > 1000) m_history.erase(m_history.begin());
}

void FPVHunterPro::updateHistoryTable() {
    if (!m_historyTable) return;
    m_historyTable->setRowCount(0);
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const auto& h = m_history[i];
        m_historyTable->insertRow(0);
        m_historyTable->setItem(0, 0, new QTableWidgetItem(h.timestamp));
        m_historyTable->setItem(0, 1, new QTableWidgetItem(QString::number(h.frequency / 1e6, 'f', 1) + " МГц"));
        m_historyTable->setItem(0, 2, new QTableWidgetItem(h.type));
        m_historyTable->setItem(0, 3, new QTableWidgetItem(QString::number(h.power, 'f', 1) + " dBFS"));
    }
}

void FPVHunterPro::updateSpectrum(double freq, double power) {
    m_spectrumData.push_back({freq, power});
    if (m_spectrumData.size() > 1000) m_spectrumData.erase(m_spectrumData.begin());
    std::vector<double> freqs, powers;
    for (const auto& d : m_spectrumData) {
        freqs.push_back(d.first / 1e6);
        powers.push_back(d.second);
    }
    m_spectrumWidget->setData(freqs, powers);
}

void FPVHunterPro::updateStatus(const QString& status) {
    statusBar()->showMessage("🔹 " + status);
}

void FPVHunterPro::updateUI() {
    QString status = m_isRecording ? "🔴 ЗАПИСЬ" : "⏸ Запись не активна";
    m_recordingStatusLabel->setText(status);
    m_recordingStatusLabel->setStyleSheet(m_isRecording ?
        QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px; font-weight: bold;")
            .arg(COLOR_WHITE).arg(COLOR_RED) :
        QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px;")
            .arg(COLOR_WHITE).arg(COLOR_DARK_GRAY));
    if (m_recordBtn) {
        m_recordBtn->setText(m_isRecording ? "⏹ СТОП ЗАПИСЬ" : "🔴 ЗАПИСЬ");
        m_recordBtn->setStyleSheet(m_isRecording ?
            QString("background-color: %1; color: %2; border: none; padding: 6px 12px; border-radius: 4px; font-weight: bold;")
                .arg(COLOR_RED).arg(COLOR_WHITE) :
            QString("background-color: %1; color: %2; border: none; padding: 6px 12px; border-radius: 4px; font-weight: bold;")
                .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    }
}

void FPVHunterPro::checkRecording() {
    QMutexLocker locker(&m_mutex);
    if (m_recordingSettings.mode == RecordingSettings::AUTO) {
        bool hasActiveVideo = false;
        for (const auto& s : m_signals) {
            if (s.hasVideo && s.isActive && s.power > m_recordingSettings.startThreshold) {
                hasActiveVideo = true;
                break;
            }
        }
        if (hasActiveVideo && !m_isRecording) startRecording(getBestSignal());
        else if (!hasActiveVideo && m_isRecording) stopRecording();
    }
}

void FPVHunterPro::onVideoExpanded(const SignalInfo& signal) {
    FullscreenVideoWindow* fullscreen = new FullscreenVideoWindow(signal, m_signals, this);
    fullscreen->showMaximized();
    m_currentVideoSignal = signal;
}

void FPVHunterPro::onVideoClicked(const SignalInfo& signal) {
    onVideoExpanded(signal);
}

void FPVHunterPro::setCurrentVideoSignal(const SignalInfo& signal) {
    m_currentVideoSignal = signal;
    statusBar()->showMessage(QString("📺 Переключено на %1 МГц").arg(signal.frequency / 1e6, 0, 'f', 1));
}

void FPVHunterPro::showSignalInfo(const SignalInfo& signal) {
    QString info = QString(
        "📡 ИНФОРМАЦИЯ О СИГНАЛЕ\n"
        "═══════════════════════════════════\n"
        "📅 Время: %1\n"
        "📶 Частота: %2 МГц\n"
        "📊 Мощность: %3 dBFS\n"
        "📌 Тип: %4\n"
        "📈 Полоса: %5 МГц\n"
        "🔄 Модуляция: %6\n"
        "📺 Стандарт: %7\n"
        "🎬 Видео: %8\n"
        "📊 Обнаружений: %9"
    )
    .arg(signal.firstSeen.toString("dd.MM.yyyy hh:mm:ss"))
    .arg(signal.frequency / 1e6, 0, 'f', 1)
    .arg(signal.power, 0, 'f', 1)
    .arg(signal.type)
    .arg(signal.bandwidth / 1e6, 0, 'f', 2)
    .arg(signal.modulation.isEmpty() ? "FM" : signal.modulation)
    .arg(signal.standard.isEmpty() ? "PAL" : signal.standard)
    .arg(signal.hasVideo ? "✅ Да" : "❌ Нет")
    .arg(signal.count);
    QMessageBox::information(this, "Информация о сигнале", info);
}

void FPVHunterPro::chooseSaveFolder() {
    QString folder = QFileDialog::getExistingDirectory(
        this, "Выберите папку для сохранения", m_recordingSettings.savePath);
    if (!folder.isEmpty()) {
        m_recordingSettings.savePath = folder;
        QDir().mkpath(folder + "/видео");
        QDir().mkpath(folder + "/снимки");
        QDir().mkpath(folder + "/отчеты");
        QDir().mkpath(folder + "/iq_samples");
        QDir().mkpath(folder + "/история");
        saveSettings();
        updateStatus(QString("📁 Папка сохранения: %1").arg(folder));
    }
}

void FPVHunterPro::showSettingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("⚙️ Настройки FPV Hunter Pro v8.0");
    dialog.setMinimumSize(700, 600);
    dialog.setStyleSheet(QString("background-color: %1; color: %2;").arg(COLOR_BLACK).arg(COLOR_WHITE));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QTabWidget* tabs = new QTabWidget();
    tabs->setStyleSheet(QString("QTabBar::tab:selected { background-color: %1; color: %2; }")
        .arg(COLOR_ORANGE).arg(COLOR_BLACK));
    
    // 1. Подключение
    QWidget* connectTab = new QWidget();
    QVBoxLayout* connectLayout = new QVBoxLayout(connectTab);
    QGroupBox* plutoGroup = new QGroupBox("🔌 Pluto+");
    QFormLayout* plutoForm = new QFormLayout();
    QLabel* statusLabel = new QLabel(m_pluto->isConnected() ? "🟢 Подключен" : "🔴 Не подключен");
    statusLabel->setStyleSheet(m_pluto->isConnected() ? 
        QString("color: %1; font-weight: bold;").arg(COLOR_GREEN) : 
        QString("color: %1; font-weight: bold;").arg(COLOR_RED));
    plutoForm->addRow("Статус:", statusLabel);
    QLineEdit* ipEdit = new QLineEdit("192.168.2.1");
    ipEdit->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid %3; padding: 4px; border-radius: 4px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_WHITE).arg(COLOR_GRAY));
    plutoForm->addRow("IP адрес:", ipEdit);
    if (m_pluto->isConnected()) {
        plutoForm->addRow("Серийный номер:", new QLabel(m_pluto->getSerial()));
        plutoForm->addRow("Модель чипа:", new QLabel(m_pluto->getChipModel()));
        plutoForm->addRow("Версия прошивки:", new QLabel(m_pluto->getFirmwareVersion()));
        plutoForm->addRow("Модель железа:", new QLabel(m_pluto->getHardwareModel()));
    }
    QPushButton* reconnectBtn = new QPushButton("🔄 Переподключить Pluto");
    reconnectBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 6px; border-radius: 4px;")
        .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    connect(reconnectBtn, &QPushButton::clicked, [this, ipEdit, statusLabel]() {
        m_pluto->disconnect();
        QString ip = ipEdit->text();
        if (m_pluto->connect(ip)) {
            statusLabel->setText("🟢 Подключен");
            statusLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(COLOR_GREEN));
        } else {
            statusLabel->setText("🔴 Не подключен");
            statusLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(COLOR_RED));
        }
        updatePlutoStatus();
    });
    plutoForm->addRow("", reconnectBtn);
    plutoGroup->setLayout(plutoForm);
    connectLayout->addWidget(plutoGroup);
    connectLayout->addStretch();
    tabs->addTab(connectTab, "🔌 Подключение");
    
    // 2. Сканирование
    QWidget* scanTab = new QWidget();
    QVBoxLayout* scanLayout = new QVBoxLayout(scanTab);
    QGroupBox* scanGroup = new QGroupBox("📡 Параметры сканирования");
    QGridLayout* scanGrid = new QGridLayout();
    int row = 0;
    scanGrid->addWidget(new QLabel("ℹ️ Диапазон частот от 100 до 6000 МГц"), row++, 0, 1, 2);
    scanGrid->addWidget(new QLabel("От (МГц):"), row, 0);
    QSpinBox* startFreqSpin = new QSpinBox(); startFreqSpin->setRange(70, 6000); startFreqSpin->setValue(m_scanSettings.startFreq / 1e6);
    scanGrid->addWidget(startFreqSpin, row++, 1);
    scanGrid->addWidget(new QLabel("До (МГц):"), row, 0);
    QSpinBox* stopFreqSpin = new QSpinBox(); stopFreqSpin->setRange(70, 6000); stopFreqSpin->setValue(m_scanSettings.stopFreq / 1e6);
    scanGrid->addWidget(stopFreqSpin, row++, 1);
    scanGrid->addWidget(new QLabel("Шаг (МГц):"), row, 0);
    QDoubleSpinBox* stepSpin = new QDoubleSpinBox(); stepSpin->setRange(0.5, 20); stepSpin->setValue(m_scanSettings.step / 1e6);
    scanGrid->addWidget(stepSpin, row++, 1);
    scanGrid->addWidget(new QLabel("Полоса (МГц):"), row, 0);
    QDoubleSpinBox* bwSpin = new QDoubleSpinBox(); bwSpin->setRange(0.5, 56); bwSpin->setValue(m_scanSettings.bandwidth / 1e6);
    scanGrid->addWidget(bwSpin, row++, 1);
    scanGrid->addWidget(new QLabel("Усиление (dB):"), row, 0);
    QSpinBox* gainSpin = new QSpinBox(); gainSpin->setRange(0, 73); gainSpin->setValue(m_scanSettings.gain);
    scanGrid->addWidget(gainSpin, row++, 1);
    QCheckBox* agcCheck = new QCheckBox("AGC (автоматическое усиление)"); agcCheck->setChecked(m_scanSettings.agcEnabled);
    scanGrid->addWidget(agcCheck, row++, 0, 1, 2);
    QCheckBox* dualCheck = new QCheckBox("Двойной режим (сканирование + видео)"); dualCheck->setChecked(m_scanSettings.dualMode);
    scanGrid->addWidget(dualCheck, row++, 0, 1, 2);
    scanGroup->setLayout(scanGrid);
    scanLayout->addWidget(scanGroup);
    QPushButton* applyScanBtn = new QPushButton("✅ Применить настройки сканирования");
    applyScanBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 8px; border-radius: 4px; font-weight: bold;")
        .arg(COLOR_GREEN).arg(COLOR_WHITE));
    connect(applyScanBtn, &QPushButton::clicked, [this, startFreqSpin, stopFreqSpin, stepSpin, bwSpin, gainSpin, agcCheck, dualCheck]() {
        m_scanSettings.startFreq = startFreqSpin->value() * 1e6;
        m_scanSettings.stopFreq = stopFreqSpin->value() * 1e6;
        m_scanSettings.step = stepSpin->value() * 1e6;
        m_scanSettings.bandwidth = bwSpin->value() * 1e6;
        m_scanSettings.gain = gainSpin->value();
        m_scanSettings.agcEnabled = agcCheck->isChecked();
        m_scanSettings.dualMode = dualCheck->isChecked();
        applyScanSettings();
        saveSettings();
        updateStatus("✅ Настройки сканирования применены");
    });
    scanLayout->addWidget(applyScanBtn);
    scanLayout->addStretch();
    tabs->addTab(scanTab, "📡 Сканирование");
    
    // 3. Видео
    QWidget* videoTab = new QWidget();
    QVBoxLayout* videoLayout = new QVBoxLayout(videoTab);
    QGroupBox* videoGroup = new QGroupBox("🎬 Настройки видео");
    QGridLayout* videoGrid = new QGridLayout();
    row = 0;
    videoGrid->addWidget(new QLabel("ℹ️ Настройки захвата и декодирования видео"), row++, 0, 1, 2);
    videoGrid->addWidget(new QLabel("Разрешение:"), row, 0);
    QComboBox* resCombo = new QComboBox(); resCombo->addItems({"480p", "720p", "1080p"});
    videoGrid->addWidget(resCombo, row++, 1);
    videoGrid->addWidget(new QLabel("FPS:"), row, 0);
    QSpinBox* fpsSpin = new QSpinBox(); fpsSpin->setRange(1, 60); fpsSpin->setValue(m_videoSettings.fps);
    videoGrid->addWidget(fpsSpin, row++, 1);
    videoGrid->addWidget(new QLabel("Кодек:"), row, 0);
    QComboBox* codecCombo = new QComboBox(); codecCombo->addItems({"X264", "X265", "VP8", "H.264", "H.265"});
    videoGrid->addWidget(codecCombo, row++, 1);
    videoGrid->addWidget(new QLabel("Битрейт (kbps):"), row, 0);
    QSpinBox* bitrateSpin = new QSpinBox(); bitrateSpin->setRange(500, 10000); bitrateSpin->setValue(m_videoSettings.bitrate);
    videoGrid->addWidget(bitrateSpin, row++, 1);
    videoGrid->addWidget(new QLabel("Формат:"), row, 0);
    QComboBox* formatCombo = new QComboBox(); formatCombo->addItems({"mp4", "avi", "mkv", "mov"});
    videoGrid->addWidget(formatCombo, row++, 1);
    QCheckBox* autoModCheck = new QCheckBox("Автоопределение FM/AM"); autoModCheck->setChecked(m_videoSettings.autoModulation);
    videoGrid->addWidget(autoModCheck, row++, 0, 1, 2);
    QCheckBox* autoStdCheck = new QCheckBox("Автоопределение PAL/NTSC"); autoStdCheck->setChecked(m_videoSettings.autoStandard);
    videoGrid->addWidget(autoStdCheck, row++, 0, 1, 2);
    videoGroup->setLayout(videoGrid);
    videoLayout->addWidget(videoGroup);
    videoLayout->addStretch();
    tabs->addTab(videoTab, "🎬 Видео");
    
    // 4. Запись
    QWidget* recordTab = new QWidget();
    QVBoxLayout* recordLayout = new QVBoxLayout(recordTab);
    QGroupBox* modeGroup = new QGroupBox("💾 Режим записи");
    QVBoxLayout* modeLayout = new QVBoxLayout();
    QRadioButton* autoRadio = new QRadioButton("Автоматическая запись (при обнаружении)");
    QRadioButton* manualRadio = new QRadioButton("Ручная запись (по кнопке)");
    if (m_recordingSettings.mode == RecordingSettings::AUTO) autoRadio->setChecked(true);
    else manualRadio->setChecked(true);
    modeLayout->addWidget(autoRadio);
    modeLayout->addWidget(manualRadio);
    modeGroup->setLayout(modeLayout);
    recordLayout->addWidget(modeGroup);
    QGroupBox* autoGroup = new QGroupBox("⚙️ Параметры авто-записи");
    QGridLayout* autoLayout = new QGridLayout();
    autoLayout->addWidget(new QLabel("Порог включения (dBFS):"), 0, 0);
    QDoubleSpinBox* startSpin = new QDoubleSpinBox(); startSpin->setRange(-80, 0); startSpin->setValue(m_recordingSettings.startThreshold); startSpin->setSuffix(" dBFS");
    autoLayout->addWidget(startSpin, 0, 1);
    autoLayout->addWidget(new QLabel("Порог выключения (dBFS):"), 1, 0);
    QDoubleSpinBox* stopSpin = new QDoubleSpinBox(); stopSpin->setRange(-80, 0); stopSpin->setValue(m_recordingSettings.stopThreshold); stopSpin->setSuffix(" dBFS");
    autoLayout->addWidget(stopSpin, 1, 1);
    autoGroup->setLayout(autoLayout);
    recordLayout->addWidget(autoGroup);
    QGroupBox* saveGroup = new QGroupBox("💾 Сохранять");
    QVBoxLayout* saveLayout = new QVBoxLayout();
    QCheckBox* saveVideoCheck = new QCheckBox("Видео"); saveVideoCheck->setChecked(m_recordingSettings.saveVideo);
    saveLayout->addWidget(saveVideoCheck);
    QCheckBox* saveIQCheck = new QCheckBox("IQ-сэмплы"); saveIQCheck->setChecked(m_recordingSettings.saveIQ);
    saveLayout->addWidget(saveIQCheck);
    QCheckBox* saveReportCheck = new QCheckBox("Отчёты"); saveReportCheck->setChecked(m_recordingSettings.saveReports);
    saveLayout->addWidget(saveReportCheck);
    saveGroup->setLayout(saveLayout);
    recordLayout->addWidget(saveGroup);
    QPushButton* saveRecordBtn = new QPushButton("💾 Сохранить настройки записи");
    saveRecordBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 8px; border-radius: 4px; font-weight: bold;")
        .arg(COLOR_GREEN).arg(COLOR_WHITE));
    connect(saveRecordBtn, &QPushButton::clicked, [this, autoRadio, manualRadio, startSpin, stopSpin, saveVideoCheck, saveIQCheck, saveReportCheck]() {
        m_recordingSettings.mode = autoRadio->isChecked() ? RecordingSettings::AUTO : RecordingSettings::MANUAL;
        m_recordingSettings.startThreshold = startSpin->value();
        m_recordingSettings.stopThreshold = stopSpin->value();
        m_recordingSettings.saveVideo = saveVideoCheck->isChecked();
        m_recordingSettings.saveIQ = saveIQCheck->isChecked();
        m_recordingSettings.saveReports = saveReportCheck->isChecked();
        saveSettings();
        updateStatus("✅ Настройки записи сохранены");
    });
    recordLayout->addWidget(saveRecordBtn);
    recordLayout->addStretch();
    tabs->addTab(recordTab, "💾 Запись");
    
    // 5. Оповещения
    QWidget* voiceTab = new QWidget();
    QVBoxLayout* voiceLayout = new QVBoxLayout(voiceTab);
    QGroupBox* voiceGroup = new QGroupBox("🔊 Голосовые и звуковые оповещения");
    QVBoxLayout* voiceGrid = new QVBoxLayout();
    QCheckBox* voiceEnableCheck = new QCheckBox("Включить голосовые оповещения"); voiceEnableCheck->setChecked(m_voiceSettings.enabled);
    voiceGrid->addWidget(voiceEnableCheck);
    QCheckBox* soundEnableCheck = new QCheckBox("Включить звуковые сигналы"); soundEnableCheck->setChecked(m_voiceSettings.soundEnabled);
    voiceGrid->addWidget(soundEnableCheck);
    QHBoxLayout* volumeLayout = new QHBoxLayout();
    volumeLayout->addWidget(new QLabel("Громкость:"));
    QSlider* volumeSlider = new QSlider(Qt::Horizontal); volumeSlider->setRange(0, 100); volumeSlider->setValue(m_voiceSettings.volume);
    volumeLayout->addWidget(volumeSlider);
    QLabel* volumeLabel = new QLabel(QString::number(m_voiceSettings.volume) + "%");
    volumeLayout->addWidget(volumeLabel);
    voiceGrid->addLayout(volumeLayout);
    voiceGrid->addWidget(new QLabel("Оповещать о:"));
    QCheckBox* notifyVideoCheck = new QCheckBox("Видео"); notifyVideoCheck->setChecked(m_voiceSettings.notifyVideo);
    voiceGrid->addWidget(notifyVideoCheck);
    QCheckBox* notifyControlsCheck = new QCheckBox("Пульты управления"); notifyControlsCheck->setChecked(m_voiceSettings.notifyControls);
    voiceGrid->addWidget(notifyControlsCheck);
    QCheckBox* notifyWiFiCheck = new QCheckBox("WiFi"); notifyWiFiCheck->setChecked(m_voiceSettings.notifyWiFi);
    voiceGrid->addWidget(notifyWiFiCheck);
    voiceGroup->setLayout(voiceGrid);
    voiceLayout->addWidget(voiceGroup);
    voiceLayout->addStretch();
    tabs->addTab(voiceTab, "🔊 Оповещения");
    
    // 6. Общие
    QWidget* generalTab = new QWidget();
    QVBoxLayout* generalLayout = new QVBoxLayout(generalTab);
    QGroupBox* folderGroup = new QGroupBox("📁 Папка сохранения");
    QHBoxLayout* folderLayout = new QHBoxLayout();
    QLineEdit* folderEdit = new QLineEdit(m_recordingSettings.savePath);
    folderEdit->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid %3; padding: 4px; border-radius: 4px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_WHITE).arg(COLOR_GRAY));
    folderLayout->addWidget(folderEdit);
    QPushButton* browseBtn = new QPushButton("📂 Обзор");
    browseBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 4px 12px; border-radius: 4px;")
        .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    connect(browseBtn, &QPushButton::clicked, [this, folderEdit]() {
        QString folder = QFileDialog::getExistingDirectory(this, "Выберите папку", folderEdit->text());
        if (!folder.isEmpty()) folderEdit->setText(folder);
    });
    folderLayout->addWidget(browseBtn);
    folderGroup->setLayout(folderLayout);
    generalLayout->addWidget(folderGroup);
    generalLayout->addStretch();
    tabs->addTab(generalTab, "📂 Общие");
    
    // 7. История
    QWidget* historyTab = new QWidget();
    QVBoxLayout* historyLayout = new QVBoxLayout(historyTab);
    QLabel* historyTitle = new QLabel("📜 История перехватов");
    historyTitle->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 14px;").arg(COLOR_ORANGE));
    historyLayout->addWidget(historyTitle);
    m_historyTable = new QTableWidget();
    m_historyTable->setColumnCount(4);
    m_historyTable->setHorizontalHeaderLabels({"Время", "Частота", "Тип", "Мощность"});
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_historyTable->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid %3;")
        .arg(COLOR_BLACK).arg(COLOR_WHITE).arg(COLOR_GRAY));
    historyLayout->addWidget(m_historyTable);
    QHBoxLayout* historyBtnLayout = new QHBoxLayout();
    QPushButton* clearHistoryBtn = new QPushButton("🗑 Очистить историю");
    clearHistoryBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 6px 12px; border-radius: 4px;")
        .arg(COLOR_RED).arg(COLOR_WHITE));
    historyBtnLayout->addWidget(clearHistoryBtn);
    QPushButton* exportHistoryBtn = new QPushButton("📤 Экспорт");
    exportHistoryBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 6px 12px; border-radius: 4px;")
        .arg(COLOR_ORANGE).arg(COLOR_WHITE));
    historyBtnLayout->addWidget(exportHistoryBtn);
    historyLayout->addLayout(historyBtnLayout);
    tabs->addTab(historyTab, "📜 История");
    
    layout->addWidget(tabs);
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* saveAllBtn = new QPushButton("💾 Сохранить все");
    saveAllBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 8px 20px; border-radius: 4px; font-weight: bold;")
        .arg(COLOR_GREEN).arg(COLOR_WHITE));
    connect(saveAllBtn, &QPushButton::clicked, [&dialog]() { dialog.accept(); });
    btnLayout->addWidget(saveAllBtn);
    QPushButton* cancelAllBtn = new QPushButton("❌ Отмена");
    cancelAllBtn->setStyleSheet(QString("background-color: %1; color: %2; border: none; padding: 8px 20px; border-radius: 4px;")
        .arg(COLOR_GRAY).arg(COLOR_WHITE));
    connect(cancelAllBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    btnLayout->addWidget(cancelAllBtn);
    layout->addLayout(btnLayout);
    dialog.setLayout(layout);
    dialog.exec();
}

void FPVHunterPro::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(3);
    mainLayout->setContentsMargins(3, 3, 3, 3);
    QHBoxLayout* topLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("🎯 FPV HUNTER PRO v8.0");
    titleLabel->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: bold;").arg(COLOR_ORANGE));
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();
    m_plutoStatusLabel = new QLabel();
    updatePlutoStatus();
    topLayout->addWidget(m_plutoStatusLabel);
    m_recordingStatusLabel = new QLabel("⏸ Запись не активна");
    m_recordingStatusLabel->setStyleSheet(QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px;")
        .arg(COLOR_WHITE).arg(COLOR_DARK_GRAY));
    topLayout->addWidget(m_recordingStatusLabel);
    QPushButton* settingsBtn = new QPushButton("⚙️ Настройки");
    settingsBtn->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px 10px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_WHITE).arg(COLOR_GRAY));
    connect(settingsBtn, &QPushButton::clicked, this, &FPVHunterPro::showSettingsDialog);
    topLayout->addWidget(settingsBtn);
    mainLayout->addLayout(topLayout);
    QHBoxLayout* contentLayout = new QHBoxLayout();
    QWidget* leftPanel = new QWidget();
    leftPanel->setMaximumWidth(280);
    leftPanel->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 6px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_GRAY));
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    QLabel* signalTitle = new QLabel("📡 ВСЕ СИГНАЛЫ");
    signalTitle->setStyleSheet(QString("color: %1; font-weight: bold; padding: 4px;").arg(COLOR_ORANGE));
    leftLayout->addWidget(signalTitle);
    m_signalList = new QListWidget();
    m_signalList->setStyleSheet(QString("background-color: %1; border: none;").arg(COLOR_BLACK));
    m_signalList->setMinimumHeight(200);
    leftLayout->addWidget(m_signalList);
    m_signalCountLabel = new QLabel("📡 Сигналов: 0");
    m_signalCountLabel->setStyleSheet(QString("color: %1; padding: 4px;").arg(COLOR_LIGHT_GRAY));
    leftLayout->addWidget(m_signalCountLabel);
    contentLayout->addWidget(leftPanel);
    QWidget* centerPanel = new QWidget();
    centerPanel->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 6px;")
        .arg(COLOR_DARK_GRAY).arg(COLOR_GRAY));
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    QLabel* videoTitle = new QLabel("📹 ВИДЕО СИГНАЛЫ");
    videoTitle->setStyleSheet(QString("color: %1; font-weight: bold; padding: 4px;").arg(COLOR_ORANGE));
    centerLayout->addWidget(videoTitle);
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: none; background-color: transparent;");
    QWidget* gridContainer = new QWidget();
    m_videoGrid = new QGridLayout(gridContainer);
    m_videoGrid->setSpacing(6);
    m_videoGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scrollArea->setWidget(gridContainer);
    centerLayout->addWidget(scrollArea);
    contentLayout->addWidget(centerPanel, 3);
    mainLayout->addLayout(contentLayout);
    QVBoxLayout* bottomLayout = new QVBoxLayout();
    bottomLayout->setSpacing(3);
    m_spectrumWidget = new SpectrumWidget();
    bottomLayout->addWidget(m_spectrumWidget);
    QHBoxLayout* indicatorLayout = new QHBoxLayout();
    m_indicatorLabel = new QLabel("📊 Ожидание сигнала...");
    m_indicatorLabel->setStyleSheet(QString("color: %1; background-color: %2; padding: 4px 8px; border-radius: 4px;")
        .arg(COLOR_LIGHT_GRAY).arg(COLOR_BLACK));
    indicatorLayout->addWidget(m_indicatorLabel);
    indicatorLayout->addStretch();
    bottomLayout->addLayout(indicatorLayout);
    mainLayout->addLayout(bottomLayout);
}

void FPVHunterPro::updateSignalList() {
    QMutexLocker locker(&m_mutex);
    m_signalList->clear();
    std::sort(m_signals.begin(), m_signals.end(), 
        [](const SignalInfo& a, const SignalInfo& b) { return a.frequency < b.frequency; });
    for (const auto& s : m_signals) {
        QString icon;
        QColor color;
        if (s.hasVideo) { icon = "🟢"; color = QColor(COLOR_GREEN); }
        else if (s.type.contains("Пульт")) { icon = "🟡"; color = QColor(COLOR_ORANGE); }
        else { icon = "⚪"; color = QColor(COLOR_WHITE); }
        QString text = QString("%1 %2 МГц | %3 | %4 dBFS")
            .arg(icon)
            .arg(s.frequency / 1e6, 0, 'f', 1)
            .arg(s.type)
            .arg(s.power, 0, 'f', 1);
        if (s.hasVideo) text += " 📹";
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setForeground(color);
        item->setToolTip(QString("Частота: %1 МГц\nТип: %2\nМощность: %3 dBFS\nМодуляция: %4\nСтандарт: %5\nОбнаружений: %6")
            .arg(s.frequency / 1e6, 0, 'f', 2)
            .arg(s.type)
            .arg(s.power, 0, 'f', 1)
            .arg(s.modulation.isEmpty() ? "FM" : s.modulation)
            .arg(s.standard.isEmpty() ? "PAL" : s.standard)
            .arg(s.count));
        m_signalList->addItem(item);
    }
    m_signalCountLabel->setText(QString("📡 Сигналов: %1").arg(m_signals.size()));
}

void FPVHunterPro::updateVideoGrid() {
    QLayoutItem* child;
    while ((child = m_videoGrid->takeAt(0)) != nullptr) {
        if (child->widget()) { child->widget()->deleteLater(); }
        delete child;
    }
    int count = 0;
    for (const auto& s : m_signals) {
        if (s.hasVideo && count < 12) {
            VideoThumbnail* thumb = new VideoThumbnail(s, this);
            thumb->setSignal(s);
            connect(thumb->getExpandBtn(), &QPushButton::clicked, [this, s]() { onVideoExpanded(s); });
            connect(thumb->getInfoBtn(), &QPushButton::clicked, [this, s]() { showSignalInfo(s); });
            m_videoGrid->addWidget(thumb);
            count++;
        }
    }
    if (count == 0) {
        QLabel* placeholder = new QLabel("📹 Ожидание видео сигналов...");
        placeholder->setStyleSheet(QString("color: %1; font-size: 14px; padding: 20px;").arg(COLOR_LIGHT_GRAY));
        placeholder->setAlignment(Qt::AlignCenter);
        m_videoGrid->addWidget(placeholder);
    }
}

void FPVHunterPro::updateSpectrumMarkers() {
    m_spectrumWidget->clearMarkers();
    for (const auto& s : m_signals) {
        SpectrumMarker marker;
        marker.frequency = s.frequency;
        marker.power = s.power;
        marker.isActive = s.isActive;
        marker.type = s.type;
        if (s.hasVideo) marker.color = QColor(COLOR_GREEN);
        else if (s.type.contains("Пульт")) marker.color = QColor(COLOR_ORANGE);
        else marker.color = QColor(COLOR_WHITE);
        m_spectrumWidget->addMarker(marker);
    }
}

SignalInfo FPVHunterPro::getBestSignal() {
    QMutexLocker locker(&m_mutex);
    SignalInfo best;
    double maxPower = -100;
    for (const auto& s : m_signals) {
        if (s.hasVideo && s.power > maxPower) {
            maxPower = s.power;
            best = s;
        }
    }
    return best;
}

void FPVHunterPro::startRecording(const SignalInfo& signal) {
    if (m_isRecording) return;
    QDir dir(m_recordingSettings.savePath + "/видео");
    if (!dir.exists() && !dir.mkpath(".")) {
        statusBar()->showMessage("❌ Ошибка создания папки для записи");
        return;
    }
    m_isRecording = true;
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString filename = QString("%1/видео/video_%2_%3.avi")
        .arg(m_recordingSettings.savePath)
        .arg((int)(signal.frequency / 1e6))
        .arg(timestamp);
    m_recordingStatusLabel->setText("🔴 ЗАПИСЬ: " + filename);
    statusBar()->showMessage("🔴 Запись начата: " + filename);
    if (m_voiceSettings.enabled) {
        m_voice->say("Запись видео начата");
    }
}

void FPVHunterPro::stopRecording() {
    if (!m_isRecording) return;
    m_isRecording = false;
    m_recordingStatusLabel->setText("⏸ Запись остановлена");
    statusBar()->showMessage("⏹ Запись остановлена");
    if (m_voiceSettings.enabled) {
        m_voice->say("Запись видео остановлена");
    }
}

void FPVHunterPro::loadSettings() {
    QSettings settings("FPVHunter", "Pro");
    m_recordingSettings.savePath = settings.value("save_path", QDir::homePath() + "/Documents/FPV_Captures").toString();
    m_recordingSettings.mode = (RecordingSettings::Mode)settings.value("record_mode", 0).toInt();
    m_recordingSettings.startThreshold = settings.value("start_threshold", -35).toDouble();
    m_recordingSettings.stopThreshold = settings.value("stop_threshold", -60).toDouble();
    m_recordingSettings.saveVideo = settings.value("save_video", true).toBool();
    m_recordingSettings.saveIQ = settings.value("save_iq", true).toBool();
    m_recordingSettings.saveReports = settings.value("save_reports", true).toBool();
    m_scanSettings.startFreq = settings.value("scan_start", 100).toDouble() * 1e6;
    m_scanSettings.stopFreq = settings.value("scan_stop", 6000).toDouble() * 1e6;
    m_scanSettings.step = settings.value("scan_step", 5).toDouble() * 1e6;
    m_scanSettings.bandwidth = settings.value("scan_bw", 4).toDouble() * 1e6;
    m_scanSettings.gain = settings.value("scan_gain", 40).toInt();
    m_scanSettings.agcEnabled = settings.value("scan_agc", false).toBool();
    m_scanSettings.dualMode = settings.value("scan_dual", false).toBool();
    m_videoSettings.resolution = settings.value("video_res", 480).toInt();
    m_videoSettings.fps = settings.value("video_fps", 25).toInt();
    m_videoSettings.bitrate = settings.value("video_bitrate", 2000).toInt();
    m_videoSettings.autoModulation = settings.value("video_auto_mod", true).toBool();
    m_videoSettings.autoStandard = settings.value("video_auto_std", true).toBool();
    m_voiceSettings.enabled = settings.value("voice_enabled", true).toBool();
    m_voiceSettings.soundEnabled = settings.value("sound_enabled", true).toBool();
    m_voiceSettings.volume = settings.value("voice_volume", 80).toInt();
    m_voiceSettings.notifyVideo = settings.value("notify_video", true).toBool();
    m_voiceSettings.notifyControls = settings.value("notify_controls", true).toBool();
    m_voiceSettings.notifyWiFi = settings.value("notify_wifi", false).toBool();
}

void FPVHunterPro::saveSettings() {
    QSettings settings("FPVHunter", "Pro");
    settings.setValue("save_path", m_recordingSettings.savePath);
    settings.setValue("record_mode", (int)m_recordingSettings.mode);
    settings.setValue("start_threshold", m_recordingSettings.startThreshold);
    settings.setValue("stop_threshold", m_recordingSettings.stopThreshold);
    settings.setValue("save_video", m_recordingSettings.saveVideo);
    settings.setValue("save_iq", m_recordingSettings.saveIQ);
    settings.setValue("save_reports", m_recordingSettings.saveReports);
    settings.setValue("scan_start", (int)(m_scanSettings.startFreq / 1e6));
    settings.setValue("scan_stop", (int)(m_scanSettings.stopFreq / 1e6));
    settings.setValue("scan_step", m_scanSettings.step / 1e6);
    settings.setValue("scan_bw", m_scanSettings.bandwidth / 1e6);
    settings.setValue("scan_gain", m_scanSettings.gain);
    settings.setValue("scan_agc", m_scanSettings.agcEnabled);
    settings.setValue("scan_dual", m_scanSettings.dualMode);
    settings.setValue("video_res", m_videoSettings.resolution);
    settings.setValue("video_fps", m_videoSettings.fps);
    settings.setValue("video_bitrate", m_videoSettings.bitrate);
    settings.setValue("video_auto_mod", m_videoSettings.autoModulation);
    settings.setValue("video_auto_std", m_videoSettings.autoStandard);
    settings.setValue("voice_enabled", m_voiceSettings.enabled);
    settings.setValue("sound_enabled", m_voiceSettings.soundEnabled);
    settings.setValue("voice_volume", m_voiceSettings.volume);
    settings.setValue("notify_video", m_voiceSettings.notifyVideo);
    settings.setValue("notify_controls", m_voiceSettings.notifyControls);
    settings.setValue("notify_wifi", m_voiceSettings.notifyWiFi);
}

QString FPVHunterPro::getStyle() {
    return QString(R"(
        QMainWindow { background-color: %1; }
        QWidget { background-color: %1; color: %2; font-family: 'Segoe UI'; }
        QListWidget { background-color: %3; border: 1px solid %4; border-radius: 6px; }
        QListWidget::item:selected { background-color: %5; color: %1; }
        QGroupBox { border: 1px solid %4; border-radius: 8px; margin-top: 12px; }
        QGroupBox::title { color: %5; padding: 0 8px; }
        QSpinBox, QDoubleSpinBox, QComboBox, QCheckBox, QLineEdit { 
            background-color: %3; border: 1px solid %4; border-radius: 4px; padding: 4px; color: %2; 
        }
        QPushButton { background-color: %3; border: 1px solid %5; border-radius: 6px; padding: 6px 12px; font-weight: bold; }
        QPushButton:hover { background-color: %5; color: %1; }
        QProgressBar { background-color: %3; border: 1px solid %4; border-radius: 4px; height: 12px; }
        QProgressBar::chunk { background-color: %6; border-radius: 4px; }
        QTabWidget::pane { border: 1px solid %4; border-radius: 6px; background-color: %1; }
        QTabBar::tab { background-color: %3; padding: 6px 16px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; color: %7; }
        QTabBar::tab:selected { background-color: %5; color: %1; }
        QTabBar::tab:hover { background-color: %4; color: %2; }
        QScrollBar:vertical { background-color: %1; width: 10px; border-radius: 4px; }
        QScrollBar::handle:vertical { background-color: %4; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background-color: %5; }
        QLabel { color: %2; }
        QTableWidget { background-color: %1; color: %2; gridline-color: %4; }
        QTableWidget::item:selected { background-color: %5; color: %1; }
        QHeaderView::section { background-color: %3; color: %2; border: 1px solid %4; padding: 4px; }
    )")
    .arg(COLOR_BLACK, COLOR_WHITE, COLOR_DARK_GRAY, COLOR_GRAY, 
         COLOR_ORANGE, COLOR_GREEN, COLOR_LIGHT_GRAY);
}

// ====================================================================
// ЗАПУСК
// ====================================================================

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setApplicationName("FPV Hunter Pro");
    app.setOrganizationName("FPVHunter");
    FPVHunterPro window;
    window.show();
    return app.exec();
}

#include "fpv_hunter_final.moc"
