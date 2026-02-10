#ifndef WIDTHPOPUP_H
#define WIDTHPOPUP_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>

// --- 1. 自定义预览区域，重写 paintEvent ---
class WidthPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WidthPreviewWidget(QWidget *parent = nullptr) : QWidget(parent), m_lineWidth(3)
    {
        setFixedSize(150, 80); // 预览区域大小
    }
    void setLineWidth(int w)
    {
        m_lineWidth = w;
        update(); // 触发重绘
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing); // 抗锯齿

        // 1. 画背景框
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(240, 240, 240));
        painter.drawRoundedRect(rect(), 8, 8);

        // 2. 画中间的预览线条 (或者画一个实心圆代表笔头)
        painter.setPen(QPen(Qt::black, m_lineWidth, Qt::SolidLine, Qt::RoundCap));

        // 在中心画一条水平线
        int y = height() / 2;
        painter.drawLine(20, y, width() - 20, y);

        // 如果你更喜欢画圆点代表笔头大小，用下面这句：
        // painter.setBrush(Qt::black); painter.setPen(Qt::NoPen);
        // painter.drawEllipse(rect().center(), m_lineWidth/2, m_lineWidth/2);
    }

private:
    int m_lineWidth;
};

// --- 2. 弹窗容器 ---
class WidthPopup : public QWidget
{
    Q_OBJECT
public:
    explicit WidthPopup(QWidget *parent = nullptr) : QWidget(parent)
    {
        //设置为 Popup 属性，这样点击窗口外部它会自动消失，像右键菜单一样
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground); // 背景透明允许圆角

        QVBoxLayout *layout = new QVBoxLayout(this);

        // 预览框
        preview = new WidthPreviewWidget(this);

        // 数值标签
        lblValue = new QLabel("3 px", this);
        lblValue->setAlignment(Qt::AlignCenter);

        // 滑动条
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(1, 50); // 笔触范围 1~50
        slider->setValue(3);

        slider->setStyleSheet(R"(
    /* 1. 滑动条整体区域和轨道背景 */
    QSlider::groove:horizontal {
        border: 0px solid #bbb;
        background: #e0e0e0; /* 轨道未激活部分的颜色 (浅灰) */
        height: 6px;         /* 轨道高度 */
        border-radius: 3px;  /* 轨道圆角，高度的一半 */
    }

    /* 2. 滑块左侧已激活的进度区域 */
    QSlider::sub-page:horizontal {
        background: #3498db; /* 进度颜色 (蓝色) */
        border-radius: 3px;  /* 与轨道保持一致 */
    }

    /* 3. 拖动手柄 (滑块) */
    QSlider::handle:horizontal {
        background: white;
        border: 2px solid #3498db; /* 蓝色边框 */
        width: 16px;  /* 滑块宽度 */
        height: 16px; /* 滑块高度 */
        border-radius: 9px; /* 宽度的一半+边框宽度，形成圆形 */
        /* 关键：负边距用于垂直居中滑块 */
        /* 计算公式大约是: (轨道高度 - 滑块高度) / 2  */
        margin: -6px 0;
    }

    /* 4. 鼠标悬停在滑块上时的状态 */
    QSlider::handle:horizontal:hover {
        background: #f0f0f0;
        border: 2px solid #2980b9; /* 边框变深一点 */
    }

    /* 5. 鼠标按下滑块时的状态 */
    QSlider::handle:horizontal:pressed {
        background: #3498db; /* 滑块变实心蓝 */
        border: 2px solid #3498db;
    }
)");

        layout->addWidget(preview);
        layout->addWidget(lblValue);
        layout->addWidget(slider);

        // 连接信号
        connect(slider, &QSlider::valueChanged, this, [=](int val){
            preview->setLineWidth(val);
            lblValue->setText(QString("%1 px").arg(val));
            emit sigLineWidthChanged(val);      // 转发信号给 Canvas
        });
    }

    void setLineWidth(int w)
    {
        slider->setValue(w);
    }

signals:
    void sigLineWidthChanged(int width);        //线条宽度改变信号

private:
    WidthPreviewWidget *preview;
    QSlider *slider;
    QLabel *lblValue;
};

#endif // WIDTHPOPUP_H
