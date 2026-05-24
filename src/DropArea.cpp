#include "DropArea.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QVBoxLayout>

DropArea::DropArea(QWidget* parent) : QFrame(parent) {
    setAcceptDrops(true);
    setFrameShape(QFrame::StyledPanel);
    setMinimumHeight(60);

    m_label = new QLabel(tr("把文件拖到这里直接发送"), this);
    m_label->setAlignment(Qt::AlignCenter);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(m_label);

    applyStyle(false);
}

void DropArea::setEnabledForDrop(bool enabled) {
    m_enabled = enabled;
    m_label->setText(enabled
        ? tr("把文件拖到这里直接发送")
        : tr("连接对端后才能拖拽发送"));
}

void DropArea::applyStyle(bool highlighted) {
    if (highlighted) {
        setStyleSheet("QFrame { background: #d6ecff; border: 2px dashed #3a8edc; border-radius: 6px; }");
    } else {
        setStyleSheet("QFrame { background: #f4f4f4; border: 2px dashed #bbbbbb; border-radius: 6px; color: #666; }");
    }
}

void DropArea::dragEnterEvent(QDragEnterEvent* e) {
    if (!m_enabled) return;
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) return;
    }
    e->acceptProposedAction();
    applyStyle(true);
    m_label->setText(tr("释放以发送 %1 个文件").arg(e->mimeData()->urls().size()));
}

void DropArea::dragLeaveEvent(QDragLeaveEvent* e) {
    Q_UNUSED(e)
    applyStyle(false);
    setEnabledForDrop(m_enabled);
}

void DropArea::dropEvent(QDropEvent* e) {
    applyStyle(false);
    setEnabledForDrop(m_enabled);
    if (!m_enabled || !e->mimeData()->hasUrls()) return;

    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) paths << u.toLocalFile();
    }
    if (!paths.isEmpty()) {
        e->acceptProposedAction();
        emit filesDropped(paths);
    }
}
