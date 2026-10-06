#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QTimer;
class ProjectDatabase;
class ScriptHighlighter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newProject();
    void openProject();
    void saveNow();
    void onTextChanged();
    void parseChangedDocument();
    void createObjectFromSelection();
    void showObjectInfo(QListWidgetItem *item = nullptr);
    void showRevisionHistory();

private:
    bool loadProject(const QString &folder);
    bool writeScriptFile();

    void installKeyboardShortcuts();
    void scanExplicitObjects();
    void rebuildMentions();
    void refreshObjects();
    void setStatus(const QString &message);

    QString scriptPath() const;
    QString databasePath() const;
    QString documentId() const;

    QPlainTextEdit *m_editor = nullptr;
    QListWidget *m_objects = nullptr;
    QPlainTextEdit *m_objectInfo = nullptr;
    QLabel *m_projectLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

    ScriptHighlighter *m_highlighter = nullptr;
    ProjectDatabase *m_db = nullptr;

    QTimer *m_autosaveTimer = nullptr;
    QTimer *m_parseTimer = nullptr;

    QString m_projectFolder;
    QString m_lastRevisionText;

    bool m_loading = false;
};
