#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QTimer>
#include <QProcessEnvironment>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief 主窗口类，负责管理界面和主要逻辑
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // 事件过滤器，用于捕获特定事件（如回车键）
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    // 点击发送按钮的槽函数
    void on_pushButton_send_clicked();
    // 处理网络数据的槽函数
    void handleReadyRead();
    // 网络请求完成后的槽函数
    void handleReply();

    // 模型选择槽函数
    void selectGSLite();
    void selectGSPro();
    void selectGSMax();
    void selectGSUltra();

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager* networkManager;  // 网络管理器
    QByteArray buffer;                      // 缓冲区用于存储流式数据
    QString accumulatedText;                // 累积AI回复的完整内容
    bool streamDone;                        // 标志流式传输是否结束
    QString currentModel;                   // 当前选择的模型名称
    QString currentPassword;                // 当前选择的模型密钥
    QMap<QString, QString> modelPasswords;  //获取环境变量中的模型密钥

    // 聊天相关
    void addMessageToChat(const QString& message, bool isUser);
    QList<QJsonObject> chatHistory;         // 存储消息历史记录
    bool aiMessageAdded;                    // 标志是否已添加 AI 消息项

    // 辅助函数
    void sendApiRequest(const QString &userInput);
    QJsonArray buildMessageArray(const QString &userInput);
    void processData(const QByteArray &jsonData);
    void updateChatMessage(const QString &content);

    //添加会话列表部分
    private:
        // 会话数据结构
        struct Conversation {
            QString title;                     // 会话标题
            QList<QJsonObject> messages;       // 消息列表
        };

        QList<Conversation> conversations;     // 所有会话列表
        int currentConversationIndex;          // 当前选中的会话索引

        // 会话管理相关方法
        void loadConversations();              // 加载会话历史
        void saveConversations();              // 保存会话历史
        void updateConversationList();         // 更新会话列表显示

private:
        void createNewConversation(const QString& firstMessage = QString());


    private slots:
        // 会话管理槽函数
        void on_newConversation_clicked();     // 新建会话
        void on_deleteConversation_clicked();  // 删除会话
        void on_conversationSelected(int index); // 选择会话
};

#endif // MAINWINDOW_H
