#ifndef MESSAGEWIDGET_H
#define MESSAGEWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>

/**
 * @brief 消息显示部件，用于显示用户和AI的消息
 */
class MessageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessageWidget(const QString& avatarPath, const QString& message, bool isUser, QWidget *parent = nullptr);
    void updateMessage(const QString& newMessage);

    // 重写 sizeHint 方法
    QSize sizeHint() const override;

private:
    QLabel* avatarLabel;    // 头像标签
    QLabel* messageLabel;   // 消息内容标签
    QHBoxLayout* layout;    // 布局
    bool isUserMessage;     // 是否为用户消息
};

#endif // MESSAGEWIDGET_H
