#pragma once

#include <QFrame>
#include <QStringList>

class QLabel;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;

// A bordered frame that highlights when files are dragged in,
// and emits filesDropped(paths) on drop.
class DropArea : public QFrame {
    Q_OBJECT
public:
    explicit DropArea(QWidget* parent = nullptr);

    void setEnabledForDrop(bool enabled);

signals:
    void filesDropped(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent     (QDropEvent* e)      override;

private:
    void applyStyle(bool highlighted);

    QLabel* m_label   = nullptr;
    bool    m_enabled = true;
};
