#include "MainWindow.h"

#include "ProjectDatabase.h"
#include "ScriptHighlighter.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCursor>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QString normalizeObjectType(const QString &raw)
{
    const QString type = raw.trimmed().toLower();

    if (type == QStringLiteral("인물") || type == QStringLiteral("character"))
        return QStringLiteral("character");
    if (type == QStringLiteral("장소") || type == QStringLiteral("location"))
        return QStringLiteral("location");
    if (type == QStringLiteral("조직") || type == QStringLiteral("organization"))
        return QStringLiteral("organization");
    if (type == QStringLiteral("사건") || type == QStringLiteral("event"))
        return QStringLiteral("event");
    if (type == QStringLiteral("물건") || type == QStringLiteral("item"))
        return QStringLiteral("item");

    return type.isEmpty() ? QStringLiteral("character") : type;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_db(new ProjectDatabase)
{
    setWindowTitle(QStringLiteral("repo 0.1"));

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_projectLabel = new QLabel(QStringLiteral("No project"), root);
    m_projectLabel->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_projectLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, root);

    m_objects = new QListWidget(splitter);
    m_editor = new QPlainTextEdit(splitter);
    m_objectInfo = new QPlainTextEdit(splitter);
    m_objectInfo->setReadOnly(true);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({220, 900, 300});

    layout->addWidget(splitter, 1);
    setCentralWidget(root);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(13);

    m_editor->setFont(mono);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setUndoRedoEnabled(true);

    m_objectInfo->setFont(mono);

    m_highlighter = new ScriptHighlighter(m_editor->document());

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(1000);

    m_parseTimer = new QTimer(this);
    m_parseTimer->setSingleShot(true);
    m_parseTimer->setInterval(250);

    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::saveNow);
    connect(m_parseTimer, &QTimer::timeout, this, &MainWindow::parseChangedDocument);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);

    connect(m_objects, &QListWidget::itemActivated,
            this, &MainWindow::showObjectInfo);

    connect(m_objects, &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                showObjectInfo(current);
            });

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("File"));

    QAction *newAction = fileMenu->addAction(QStringLiteral("New Project"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);

    QAction *openAction = fileMenu->addAction(QStringLiteral("Open Project"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);

    QAction *saveAction = fileMenu->addAction(QStringLiteral("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveNow);

    auto *editMenu = menuBar()->addMenu(QStringLiteral("Edit"));

    QAction *undoAction = editMenu->addAction(QStringLiteral("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, m_editor, &QPlainTextEdit::undo);

    QAction *redoAction = editMenu->addAction(QStringLiteral("Redo"));
#if defined(Q_OS_MACOS)
    redoAction->setShortcut(QKeySequence(Qt::META | Qt::Key_U));
#else
    redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
#endif
    connect(redoAction, &QAction::triggered, m_editor, &QPlainTextEdit::redo);

    QAction *createObjectAction =
        editMenu->addAction(QStringLiteral("Create Object from Selection"));
#if defined(Q_OS_MACOS)
    createObjectAction->setShortcut(QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_O));
#else
    createObjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
#endif
    connect(createObjectAction, &QAction::triggered,
            this, &MainWindow::createObjectFromSelection);

    QAction *historyAction =
        editMenu->addAction(QStringLiteral("Revision History"));
#if defined(Q_OS_MACOS)
    historyAction->setShortcut(QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_H));
#else
    historyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
#endif
    connect(historyAction, &QAction::triggered,
            this, &MainWindow::showRevisionHistory);

    m_statusLabel = new QLabel(QStringLiteral("Ready"), this);
    statusBar()->addWidget(m_statusLabel, 1);

    installKeyboardShortcuts();
    m_editor->setFocus();
}

MainWindow::~MainWindow()
{
    delete m_db;
}

void MainWindow::installKeyboardShortcuts()
{
    auto *focusEditor = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    focusEditor->setContext(Qt::ApplicationShortcut);

    connect(focusEditor, &QShortcut::activated,
            m_editor, QOverload<>::of(&QWidget::setFocus));
}

void MainWindow::newProject()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString parent =
        QFileDialog::getExistingDirectory(this,
                                          QStringLiteral("Choose parent folder"),
                                          base);

    if (parent.isEmpty()) return;

    bool ok = false;
    const QString name =
        QInputDialog::getText(this,
                              QStringLiteral("New Project"),
                              QStringLiteral("Project name:"),
                              QLineEdit::Normal,
                              QStringLiteral("NewProject"),
                              &ok).trimmed();

    if (!ok || name.isEmpty()) return;

    QDir parentDir(parent);
    const QString folder = parentDir.filePath(name);

    if (!QDir().mkpath(folder)) {
        QMessageBox::critical(this,
                              QStringLiteral("Error"),
                              QStringLiteral("Could not create project folder."));
        return;
    }

    loadProject(folder);
}

void MainWindow::openProject()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString folder =
        QFileDialog::getExistingDirectory(this,
                                          QStringLiteral("Open repo project"),
                                          base);

    if (!folder.isEmpty())
        loadProject(folder);
}

bool MainWindow::loadProject(const QString &folder)
{
    if (!m_projectFolder.isEmpty())
        saveNow();

    QDir dir(folder);
    if (!dir.exists()) return false;

    if (!dir.exists(QStringLiteral("script")) &&
        !dir.mkdir(QStringLiteral("script"))) {
        QMessageBox::critical(this,
                              QStringLiteral("Error"),
                              QStringLiteral("Could not create script folder."));
        return false;
    }

    QString error;
    if (!m_db->open(dir.filePath(QStringLiteral("project.sqlite")), &error)) {
        QMessageBox::critical(this,
                              QStringLiteral("Database error"),
                              error);
        return false;
    }

    m_projectFolder = dir.absolutePath();
    m_projectLabel->setText(m_projectFolder);

    m_loading = true;

    QFile script(scriptPath());
    if (script.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_editor->setPlainText(QString::fromUtf8(script.readAll()));
        script.close();
    } else {
        m_editor->setPlainText(QStringLiteral(
            "# 첫 장면\n\n"
            "## 상황 설명을 입력하세요. ##\n\n"
            "[[김철수]]\n"
            "@@ 대사를 입력하세요. @@\n\n"
            "₩₩ 나레이션을 입력하세요. ₩₩\n"));
    }

    m_loading = false;

    scanExplicitObjects();
    rebuildMentions();
    refreshObjects();

    m_lastRevisionText = m_editor->toPlainText();

    writeScriptFile();
    m_db->addRevision(documentId(),
                      m_lastRevisionText,
                      QStringLiteral("open"));

    setStatus(QStringLiteral("Project loaded"));
    m_editor->setFocus();

    return true;
}

QString MainWindow::scriptPath() const
{
    if (m_projectFolder.isEmpty()) return {};

    return QDir(m_projectFolder).filePath(
        QStringLiteral("script/main.txt"));
}

QString MainWindow::databasePath() const
{
    if (m_projectFolder.isEmpty()) return {};

    return QDir(m_projectFolder).filePath(
        QStringLiteral("project.sqlite"));
}

QString MainWindow::documentId() const
{
    return QStringLiteral("script/main.txt");
}

bool MainWindow::writeScriptFile()
{
    if (m_projectFolder.isEmpty()) return false;

    QDir dir(m_projectFolder);
    if (!dir.exists(QStringLiteral("script")) &&
        !dir.mkdir(QStringLiteral("script"))) {
        return false;
    }

    QFile file(scriptPath());
    if (!file.open(QIODevice::WriteOnly |
                   QIODevice::Truncate |
                   QIODevice::Text)) {
        return false;
    }

    const QByteArray data = m_editor->toPlainText().toUtf8();

    if (file.write(data) != data.size()) {
        file.close();
        return false;
    }

    file.close();
    return true;
}

void MainWindow::saveNow()
{
    if (m_projectFolder.isEmpty() || m_loading)
        return;

    if (!writeScriptFile()) {
        setStatus(QStringLiteral("Save failed"));
        return;
    }

    const QString text = m_editor->toPlainText();

    if (text != m_lastRevisionText) {
        QString error;

        if (m_db->addRevision(documentId(),
                              text,
                              QStringLiteral("autosave"),
                              &error)) {
            m_lastRevisionText = text;
            m_db->pruneRevisions(documentId(), 500);
        } else {
            setStatus(QStringLiteral("Revision save failed: %1").arg(error));
            return;
        }
    }

    setStatus(QStringLiteral("Saved"));
}

void MainWindow::onTextChanged()
{
    if (m_loading) return;

    m_autosaveTimer->start();
    m_parseTimer->start();
}

void MainWindow::parseChangedDocument()
{
    if (m_projectFolder.isEmpty() || !m_db->isOpen())
        return;

    scanExplicitObjects();
    rebuildMentions();
    refreshObjects();

    m_highlighter->setObjectLexicon(m_db->objectLexicon());
}

void MainWindow::scanExplicitObjects()
{
    if (!m_db->isOpen()) return;

    const QString text = m_editor->toPlainText();

    const QRegularExpression expression(
        QStringLiteral(
            "\\[\\[(?:(?<type>[^:\\]\\|]+):)?"
            "(?<name>[^\\]\\|]+)"
            "(?:\\|[^\\]]+)?\\]\\]"));

    QRegularExpressionMatchIterator matches =
        expression.globalMatch(text);

    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();

        const QString name =
            match.captured(QStringLiteral("name")).trimmed();

        const QString type =
            normalizeObjectType(
                match.captured(QStringLiteral("type")));

        if (!name.isEmpty())
            m_db->ensureObject(name, type);
    }
}

void MainWindow::rebuildMentions()
{
    if (!m_db->isOpen()) return;

    const QString text = m_editor->toPlainText();
    const QHash<QString, qint64> lexicon = m_db->objectLexicon();

    QVector<QPair<qint64, int>> mentions;

    for (auto it = lexicon.constBegin();
         it != lexicon.constEnd();
         ++it) {
        const QString &name = it.key();
        if (name.isEmpty()) continue;

        int offset = 0;

        while ((offset = text.indexOf(name,
                                      offset,
                                      Qt::CaseSensitive)) >= 0) {
            mentions.push_back(qMakePair(it.value(), offset));
            offset += qMax(1, name.size());
        }
    }

    QString error;
    if (!m_db->replaceMentions(documentId(), mentions, &error))
        setStatus(QStringLiteral("Mention index failed: %1").arg(error));
}

void MainWindow::refreshObjects()
{
    if (!m_db->isOpen()) return;

    qint64 selectedId = 0;

    if (QListWidgetItem *current = m_objects->currentItem())
        selectedId = current->data(Qt::UserRole).toLongLong();

    m_objects->clear();

    const QVector<ObjectRecord> objects = m_db->objects();

    QListWidgetItem *itemToRestore = nullptr;

    for (const ObjectRecord &object : objects) {
        auto *item =
            new QListWidgetItem(
                QStringLiteral("%1  [%2]")
                    .arg(object.name, object.type),
                m_objects);

        item->setData(Qt::UserRole, object.id);

        if (object.id == selectedId)
            itemToRestore = item;
    }

    if (itemToRestore)
        m_objects->setCurrentItem(itemToRestore);
    else if (m_objects->count() > 0 && !m_objects->currentItem())
        m_objects->setCurrentRow(0);

    m_highlighter->setObjectLexicon(m_db->objectLexicon());
}

void MainWindow::createObjectFromSelection()
{
    if (m_projectFolder.isEmpty() || !m_db->isOpen()) {
        setStatus(QStringLiteral("Open a project first"));
        return;
    }

    QTextCursor cursor = m_editor->textCursor();

    QString name = cursor.selectedText().trimmed();

    if (name.isEmpty()) {
        cursor.select(QTextCursor::WordUnderCursor);
        name = cursor.selectedText().trimmed();
    }

    name.remove(QStringLiteral("[["));
    name.remove(QStringLiteral("]]"));

    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(
                   this,
                   QStringLiteral("Create Object"),
                   QStringLiteral("Object name:"),
                   QLineEdit::Normal,
                   QString(),
                   &ok).trimmed();

        if (!ok || name.isEmpty())
            return;
    }

    const qint64 id =
        m_db->ensureObject(name, QStringLiteral("character"));

    if (id <= 0) {
        setStatus(QStringLiteral("Could not create object"));
        return;
    }

    rebuildMentions();
    refreshObjects();
    setStatus(QStringLiteral("Object registered: %1").arg(name));
    m_editor->setFocus();
}

void MainWindow::showObjectInfo(QListWidgetItem *item)
{
    if (!item || !m_db->isOpen()) {
        m_objectInfo->clear();
        return;
    }

    const qint64 id =
        item->data(Qt::UserRole).toLongLong();

    const QVector<ObjectRecord> objects = m_db->objects();

    for (const ObjectRecord &object : objects) {
        if (object.id != id) continue;

        QString info;

        info += QStringLiteral("Name\n%1\n\n")
                    .arg(object.name);

        info += QStringLiteral("Type\n%1\n\n")
                    .arg(object.type);

        if (!object.aliases.isEmpty()) {
            info += QStringLiteral("Aliases\n%1\n\n")
                        .arg(object.aliases.join(QStringLiteral(", ")));
        }

        info += QStringLiteral("Mentions\n%1\n")
                    .arg(m_db->mentionCount(object.id));

        m_objectInfo->setPlainText(info);
        return;
    }

    m_objectInfo->clear();
}

void MainWindow::showRevisionHistory()
{
    if (m_projectFolder.isEmpty() || !m_db->isOpen())
        return;

    const QVector<RevisionRecord> revisions =
        m_db->revisions(documentId(), 100);

    if (revisions.isEmpty()) {
        setStatus(QStringLiteral("No revisions"));
        return;
    }

    QStringList labels;
    labels.reserve(revisions.size());

    for (const RevisionRecord &revision : revisions) {
        labels.push_back(
            QStringLiteral("#%1  %2  %3")
                .arg(revision.id)
                .arg(revision.createdAt,
                     revision.reason));
    }

    bool ok = false;
    const QString selected =
        QInputDialog::getItem(this,
                              QStringLiteral("Revision History"),
                              QStringLiteral("Choose a revision to restore:"),
                              labels,
                              0,
                              false,
                              &ok);

    if (!ok || selected.isEmpty())
        return;

    const int index = labels.indexOf(selected);
    if (index < 0 || index >= revisions.size())
        return;

    QString error;
    const QString text =
        m_db->revisionText(revisions.at(index).id, &error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this,
                              QStringLiteral("Revision error"),
                              error);
        return;
    }

    const QMessageBox::StandardButton result =
        QMessageBox::question(
            this,
            QStringLiteral("Restore Revision"),
            QStringLiteral("Restore revision #%1?")
                .arg(revisions.at(index).id),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    m_loading = true;
    m_editor->setPlainText(text);
    m_loading = false;

    scanExplicitObjects();
    rebuildMentions();
    refreshObjects();

    writeScriptFile();

    m_db->addRevision(documentId(),
                      text,
                      QStringLiteral("restore"));

    m_lastRevisionText = text;

    setStatus(QStringLiteral("Revision restored"));
    m_editor->setFocus();
}

void MainWindow::setStatus(const QString &message)
{
    m_statusLabel->setText(message);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveNow();
    event->accept();
}
