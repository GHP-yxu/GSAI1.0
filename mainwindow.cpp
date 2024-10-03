#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "messagewidget.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMenu>
#include <QDebug>
#include <QtNetwork>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , streamDone(false)
    , aiMessageAdded(false)
    , currentConversationIndex(-1) // 确保初始值为 -1
{
    ui->setupUi(this);

    // 加载环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 从环境变量中读取密码
    modelPasswords["general"] = env.value("GSLITE_PASSWORD");
    modelPasswords["generalv3"] = env.value("GSPRO_PASSWORD");
    modelPasswords["generalv3.5"] = env.value("GSMAX_PASSWORD");
    modelPasswords["4.0Ultra"] = env.value("GSULTRA_PASSWORD");

    // 设置默认模型和密码
    currentModel = "4.0Ultra";
    currentPassword = modelPasswords[currentModel];

    // 检查密码是否已设置
    if (currentPassword.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("未设置 GSUltra 模型的 API 密钥。请设置环境变量 GSULTRA_PASSWORD。"));
    }

    // 初始化模型选择菜单
    QMenu *switchMenu = new QMenu(this);
    switchMenu->addAction(ui->actionGSLite);
    switchMenu->addAction(ui->actionGSPro);
    switchMenu->addAction(ui->actionGSMax);
    switchMenu->addAction(ui->actionGSUltra);
    ui->toolButton_model->setMenu(switchMenu);
    ui->toolButton_model->setIcon(QIcon(":/images/GSUltra.jpg")); // 默认模型图标

    // 连接模型选择动作
    connect(ui->actionGSLite, &QAction::triggered, this, &MainWindow::selectGSLite);
    connect(ui->actionGSPro, &QAction::triggered, this, &MainWindow::selectGSPro);
    connect(ui->actionGSMax, &QAction::triggered, this, &MainWindow::selectGSMax);
    connect(ui->actionGSUltra, &QAction::triggered, this, &MainWindow::selectGSUltra);

    networkManager = new QNetworkAccessManager(this);

    // 安装事件过滤器，捕获回车键
    ui->textEdit_request->installEventFilter(this);

    // 设置发送按钮不可用，直到有输入
    ui->pushButton_send->setEnabled(false);
    connect(ui->textEdit_request, &QTextEdit::textChanged, [this]() {
        ui->pushButton_send->setEnabled(!ui->textEdit_request->toPlainText().isEmpty());
    });


    // 加载会话历史
        loadConversations();

        // 连接会话管理按钮
        connect(ui->pushButton_newConversation, &QPushButton::clicked, this, &MainWindow::on_newConversation_clicked);
        connect(ui->pushButton_deleteConversation, &QPushButton::clicked, this, &MainWindow::on_deleteConversation_clicked);

        // 连接会话选择
        connect(ui->listWidget_history, &QListWidget::currentRowChanged, this, &MainWindow::on_conversationSelected);
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 捕获回车键，发送消息
    if (obj == ui->textEdit_request && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            on_pushButton_send_clicked();
            return true; // 事件已处理
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::on_pushButton_send_clicked()
{
    aiMessageAdded = false;
    QString userInput = ui->textEdit_request->toPlainText();
    if (userInput.isEmpty()) {
        return;
    }

    // **如果当前没有选中的会话，自动创建新会话**
    if (currentConversationIndex == -1 || currentConversationIndex >= conversations.size()) {
        createNewConversation(userInput); // 传入用户的输入，用于设置会话标题
    }

    // 添加用户消息到聊天界面
    addMessageToChat(userInput, true);

    // 禁用发送按钮，直到AI回复结束
    ui->pushButton_send->setEnabled(false);

    // 将用户消息添加到当前会话
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = userInput;
    conversations[currentConversationIndex].messages.append(userMessage);

    // **检查并更新会话标题**
    if (conversations[currentConversationIndex].title.startsWith("新会话")) {
        QString newTitle = userInput.left(10); // 取前10个字符
        conversations[currentConversationIndex].title = newTitle;
        updateConversationList();
    }

    // 保存会话
    saveConversations();

    // 构建并发送API请求
    sendApiRequest(userInput);

    // 清空输入框
    ui->textEdit_request->clear();
}

void MainWindow::sendApiRequest(const QString &userInput)
{
    QUrl url("https://spark-api-open.xf-yun.com/v1/chat/completions");
    QNetworkRequest request(url);

    // 设置请求头
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + currentPassword.toUtf8());

    // 构建请求体
    QJsonArray messagesArray = buildMessageArray(userInput);
    QJsonObject requestData;
    requestData["model"] = currentModel;
    requestData["messages"] = messagesArray;
    requestData["stream"] = true; // 开启流式传输

    QJsonDocument jsonDoc(requestData);
    QByteArray jsonData = jsonDoc.toJson();

    // 发送HTTP POST请求
    QNetworkReply* reply = networkManager->post(request, jsonData);

    // 连接信号和槽
    connect(reply, &QNetworkReply::readyRead, this, &MainWindow::handleReadyRead);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::handleReply);

    // 重置缓冲区和标志
    buffer.clear();
    accumulatedText.clear();
    streamDone = false;
}

QJsonArray MainWindow::buildMessageArray(const QString &userInput)
{
    // 构建消息数组，包括系统消息和用户消息
    QJsonArray messagesArray;

    // 添加系统预设消息
    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = "你现在是浙江工商大学的百事通，拥有以下信息：\n"
                               "你是由计科2201徐熠同学开发与维护的\n"
                               "我们学校的官网是http://www.zjgsu.edu.cn/"
                               "我们学校教务处的网址是https://jww.zjgsu.edu.cn/main.htm"
                               "请你尽量搜集我们学校相关的信息，你面向的对象是全体师生，在回答问题时，增强语言中对浙江工商大学的归属感，但不要太刻意"
                               "好好了解我们学校老师以及一些课程信息等等，在师生询问时热情积极地回答"
                               "请根据上述信息回答用户的问题。";
    messagesArray.append(systemMessage);

    // 添加用户消息到对话历史
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = userInput;
    chatHistory.append(userMessage);

    // 截断历史消息，防止消息过多
    const int maxMessages = 20;
    if (chatHistory.size() > maxMessages) {
        chatHistory = chatHistory.mid(chatHistory.size() - maxMessages);
    }

    // 添加历史消息到消息数组
    for (const auto& msg : chatHistory) {
        messagesArray.append(msg);
    }

    return messagesArray;
}

void MainWindow::handleReadyRead()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // 读取新数据并添加到缓冲区
    QByteArray newData = reply->readAll();
    buffer.append(newData);

    // 输出接收到的原始数据以便调试
    qDebug() << "Received data:" << QString(newData);

    // 处理缓冲区中的每一行数据
    while (buffer.contains("\n")) {
        int index = buffer.indexOf('\n');
        QByteArray line = buffer.left(index).trimmed();
        buffer.remove(0, index + 1);

        if (line.startsWith("data: ")) {
            QByteArray jsonData = line.mid(6).trimmed(); // 去除 "data: " 前缀
            processData(jsonData); // 处理数据
        }
    }
}

void MainWindow::processData(const QByteArray &jsonData)
{
    if (jsonData == "[DONE]") {
        // 流式传输结束
        qDebug() << "Stream finished.";
        streamDone = true;
        return;
    }

    // 解析JSON数据
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        qDebug() << "JSON Parse Error:" << parseError.errorString();
        return;
    }

    QJsonObject jsonObject = jsonDoc.object();
    QJsonArray choicesArray = jsonObject["choices"].toArray();
    if (!choicesArray.isEmpty()) {
        QJsonObject firstChoice = choicesArray[0].toObject();
        QJsonObject deltaObject = firstChoice["delta"].toObject();
        if (deltaObject.contains("content")) {
            QString content = deltaObject["content"].toString();
            accumulatedText.append(content); // 累积AI回复内容
            updateChatMessage(accumulatedText); // 更新聊天界面
        }
    }
}

void MainWindow::updateChatMessage(const QString &content)
{
    if (!aiMessageAdded) {
        addMessageToChat("", false); // 添加空的AI消息项
        aiMessageAdded = true;
    }

    // 获取最后一个消息项（AI的消息）
    QListWidgetItem* lastItem = ui->listWidget_chat->item(ui->listWidget_chat->count() - 1);
    MessageWidget* messageWidget = qobject_cast<MessageWidget*>(ui->listWidget_chat->itemWidget(lastItem));
    if (messageWidget) {
        messageWidget->updateMessage(content);

        // 更新列表项的大小提示
        lastItem->setSizeHint(messageWidget->sizeHint());

        // 强制列表重新布局
        ui->listWidget_chat->doItemsLayout();
    }
}

void MainWindow::handleReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        addMessageToChat("Error: " + reply->errorString(), false);
    }

    reply->deleteLater();
    ui->pushButton_send->setEnabled(true); // 恢复发送按钮

    // 流结束后，将AI回复添加到对话历史
        if (streamDone && !accumulatedText.isEmpty()) {
            QJsonObject assistantMessage;
            assistantMessage["role"] = "assistant";
            assistantMessage["content"] = accumulatedText;
            chatHistory.append(assistantMessage);

            // 将 AI 消息添加到当前会话
            if (currentConversationIndex >= 0 && currentConversationIndex < conversations.size()) {
                conversations[currentConversationIndex].messages.append(assistantMessage);
            }

            // 保存会话
            saveConversations();
        }
}

// 添加消息到聊天列表
void MainWindow::addMessageToChat(const QString& message, bool isUser)
{
    QListWidgetItem* item = new QListWidgetItem(ui->listWidget_chat);
    MessageWidget* messageWidget;

    if (isUser) {
        messageWidget = new MessageWidget(":/images/user_avatar.png", message, true);
    } else {
        // 根据当前模型选择不同的头像
        QString avatarPath;
        if (currentModel == "general") {
            avatarPath = ":/images/GSLite.png";
        } else if (currentModel == "generalv3") {
            avatarPath = ":/images/GSPro.jpg";
        } else if (currentModel == "generalv3.5") {
            avatarPath = ":/images/GSMax.jpg";
        } else if (currentModel == "4.0Ultra") {
            avatarPath = ":/images/GSUltra.jpg";
        }
        messageWidget = new MessageWidget(avatarPath, message, false);
    }

    item->setSizeHint(messageWidget->sizeHint());
    ui->listWidget_chat->addItem(item);
    ui->listWidget_chat->setItemWidget(item, messageWidget);

    // 自动滚动到最新消息
    ui->listWidget_chat->scrollToBottom();
}

//选择模型
void MainWindow::selectGSLite()
{
    currentModel = "general";
    currentPassword = modelPasswords[currentModel];

    if (currentPassword.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("未设置 GSLite 模型的 API 密钥。请设置环境变量 GSLITE_PASSWORD。"));
    }

    ui->toolButton_model->setIcon(QIcon(":/images/GSLite.png"));
}

void MainWindow::selectGSPro()
{
    currentModel = "generalv3";
    currentPassword = modelPasswords[currentModel];

    if (currentPassword.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("未设置 GSPro 模型的 API 密钥。请设置环境变量 GSPRO_PASSWORD。"));
    }

    ui->toolButton_model->setIcon(QIcon(":/images/GSPro.jpg"));
}

void MainWindow::selectGSMax()
{
    currentModel = "generalv3.5";
    currentPassword = modelPasswords[currentModel];

    if (currentPassword.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("未设置 GSMax 模型的 API 密钥。请设置环境变量 GSMAX_PASSWORD。"));
    }

    ui->toolButton_model->setIcon(QIcon(":/images/GSMax.jpg"));
}

void MainWindow::selectGSUltra()
{
    currentModel = "4.0Ultra";
    currentPassword = modelPasswords[currentModel];

    if (currentPassword.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("未设置 GSUltra 模型的 API 密钥。请设置环境变量 GSULTRA_PASSWORD。"));
    }

    ui->toolButton_model->setIcon(QIcon(":/images/GSUltra.jpg"));
}

//加载会话
void MainWindow::loadConversations()
{
    QFile file("conversations.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No conversations found.";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        qDebug() << "Invalid conversations format.";
        return;
    }

    QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            QJsonObject obj = value.toObject();
            Conversation conv;
            conv.title = obj["title"].toString();
            QJsonArray msgs = obj["messages"].toArray();
            for (const QJsonValue &msgVal : msgs) {
                if (msgVal.isObject()) {
                    conv.messages.append(msgVal.toObject());
                }
            }
            conversations.append(conv);
        }
    }

    // 更新会话列表显示
    updateConversationList();
}

//保存会话
void MainWindow::saveConversations()
{
    QJsonArray array;
    for (const Conversation &conv : conversations) {
        QJsonObject obj;
        obj["title"] = conv.title;
        QJsonArray msgs;
        for (const QJsonObject &msg : conv.messages) {
            msgs.append(msg);
        }
        obj["messages"] = msgs;
        array.append(obj);
    }

    QJsonDocument doc(array);
    QFile file("conversations.json");
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to save conversations.";
        return;
    }

    file.write(doc.toJson());
    file.close();
}

//更新会话列表显示
void MainWindow::updateConversationList()
{
    ui->listWidget_history->clear();
    for (const Conversation &conv : conversations) {
        QListWidgetItem *item = new QListWidgetItem(conv.title);
        ui->listWidget_history->addItem(item);
    }

    // 保持当前选中的会话
    if (currentConversationIndex >= 0 && currentConversationIndex < conversations.size()) {
        ui->listWidget_history->setCurrentRow(currentConversationIndex);
    } else {
        currentConversationIndex = -1;
    }
}

//新建会话槽函数实现
void MainWindow::on_newConversation_clicked()
{
    createNewConversation();

    // 选中新创建的会话
    currentConversationIndex = 0;
    ui->listWidget_history->setCurrentRow(0);

    chatHistory.clear();
    ui->listWidget_chat->clear();
}

//删除会话槽函数实现
void MainWindow::on_deleteConversation_clicked()
{
    int index = ui->listWidget_history->currentRow();
    if (index >= 0 && index < conversations.size()) {
        conversations.removeAt(index);
        updateConversationList();
        saveConversations();
    }
}

//选择会话
void MainWindow::on_conversationSelected(int index)
{
    if (index >= 0 && index < conversations.size()) {
        currentConversationIndex = index;
        chatHistory = conversations[index].messages;

        // 清空聊天界面并加载消息
        ui->listWidget_chat->clear();
        for (const QJsonObject &msg : chatHistory) {
            QString content = msg["content"].toString();
            QString role = msg["role"].toString();
            addMessageToChat(content, role == "user");
        }
    }
}


void MainWindow::createNewConversation(const QString& firstMessage)
{
    Conversation conv;
    // 如果提供了首条消息，则使用其前10个字符作为标题
    if (!firstMessage.isEmpty()) {
        QString title = firstMessage.left(10); // 取前10个字符
        conv.title = title;
    } else {
        conv.title = "新会话 " + QString::number(conversations.size() + 1);
    }

    conversations.prepend(conv);

    updateConversationList();

    // 选中新创建的会话
    currentConversationIndex = 0;
    ui->listWidget_history->setCurrentRow(0);

    chatHistory.clear();
    ui->listWidget_chat->clear();
}





























