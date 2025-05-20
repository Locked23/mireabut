#include <stdio.h>
#include <tgbot/tgbot.h>
#include <sqlite3.h>
#include <string>
#include <map>
#include <unordered_map>

//конфиг
const std::string BOT_TOKEN = "";
const int64_t SUPPORT_CHAT_ID = -1002265323364;
const std::string OPERATOR = "@Lockeddd23";

//структура под бд
struct BugReport {
    int id;
    int64_t user_id;
    std::string user_name;
    std::string message;
    int message_id_in_support;
    std::string status; // "open", "in_progress", "closed"
    std::string support_response;
};

//класс для базы данных и работы в tgbot 
class Database {
private:
    sqlite3* db;

public:
    Database(const std::string& db_name) {
        if (sqlite3_open(db_name.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Can't open database: " + std::string(sqlite3_errmsg(db)));
        }
        create_tables();
    }

    ~Database() {
        sqlite3_close(db);
    }
    //пустая функция (функция которая ничего не возвращает) которая создает таблицу в SQL бд
    void create_tables() {
        const char* sql = "CREATE TABLE IF NOT EXISTS reports ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "user_name TEXT NOT NULL,"
            "message TEXT NOT NULL,"
            "message_id_in_support INTEGER NOT NULL,"
            "status TEXT DEFAULT 'open',"
            "support_response TEXT DEFAULT '',"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);";

        char* errMsg = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::string error = "SQL error: " + std::string(errMsg);
            sqlite3_free(errMsg);
            throw std::runtime_error(error);
        }
    }
    //метод класса database которая добавляет данные user_id, user_name, message, message_id_in_support в SQL бд 
    int add_report(const BugReport& report) {
        const char* sql = "INSERT INTO reports (user_id, user_name, message, message_id_in_support) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare statement");
        }

        sqlite3_bind_int64(stmt, 1, report.user_id);
        sqlite3_bind_text(stmt, 2, report.user_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, report.message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, report.message_id_in_support);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement");
        }

        int id = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(stmt);
        return id;
    }
    //метод класса который обновляет статус после ответа оператора
    void update_report_response(int report_id, const std::string& response) {
        const char* sql = "UPDATE reports SET support_response = ?, status = 'closed' WHERE id = ?;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare statement");
        }

        sqlite3_bind_text(stmt, 1, response.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, report_id);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement");
        }

        sqlite3_finalize(stmt);
    }
    //метод который в будущем будет вызываться в main() функции. Выполняет обработку get репортов
    BugReport get_report(int report_id) {
        const char* sql = "SELECT * FROM reports WHERE id = ?;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare statement");
        }

        sqlite3_bind_int(stmt, 1, report_id);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Report not found");
        }

        BugReport report;
        report.id = sqlite3_column_int(stmt, 0);
        report.user_id = sqlite3_column_int64(stmt, 1);
        report.user_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        report.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        report.message_id_in_support = sqlite3_column_int(stmt, 4);
        report.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        report.support_response = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

        sqlite3_finalize(stmt);
        return report;
    }
    //метод который в будущем будет вызываться в main() функции. Выполняет обработку /reports по статусу
    std::vector<BugReport> get_open_reports() {
        const char* sql = "SELECT * FROM reports WHERE status = 'open';";
        std::vector<BugReport> reports;

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare statement");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BugReport report;
            report.id = sqlite3_column_int(stmt, 0);
            report.user_id = sqlite3_column_int64(stmt, 1);
            report.user_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            report.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            report.message_id_in_support = sqlite3_column_int(stmt, 4);
            report.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            report.support_response = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

            reports.push_back(report);
        }

        sqlite3_finalize(stmt);
        return reports;
    }
};

int main() {
    try {
        //создание базы данных при помощи класса Database
        Database db("bug_reports.db");
        TgBot::Bot bot(BOT_TOKEN);

        std::unordered_map<int64_t, bool> waitingForReport;
        std::unordered_map<int64_t, int> waitingForReportResponse;

        // обработчик start 
        bot.getEvents().onCommand("start", [&bot, &waitingForReport](TgBot::Message::Ptr message) {
            TgBot::ReplyKeyboardMarkup::Ptr keyboard(new TgBot::ReplyKeyboardMarkup);
            keyboard->resizeKeyboard = true;
            keyboard->oneTimeKeyboard = false;
            //Если чат не является чатом поддержки то инициализируется кнопка Отправить сообщение в поддержку
            if (message->chat->id != SUPPORT_CHAT_ID) {
                TgBot::KeyboardButton::Ptr rprtButton(new TgBot::KeyboardButton);
                rprtButton->text = u8"Отправить сообщение в поддержку";
                std::vector<TgBot::KeyboardButton::Ptr> row;
                row.push_back(rprtButton);
                keyboard->keyboard.push_back(row);
            }

            std::string welcome = u8"Добро пожаловать!";
            if (message->chat->id == SUPPORT_CHAT_ID) {
                //Если чат является чатом поддержки условие выполняется 
                welcome += u8"\n [⚒️Staff⚒️] \n Вы в чате поддержки. Доступные команды:\n"
                    u8"/reports - показать неотвеченные репорты\n"
                    u8"/reply [номер] - ответить на конкретный репорт\n"
                    u8"Также можно отвечать на репорты свайпнув влево либо ПКМ.";

                welcome += u8"\nЧат поддержки: " + SUPPORT_CHAT_LINK;
            }
            //иначе пользовательское приветствие
            else {
                welcome += u8"\nНажмите кнопку ниже, чтобы отправить баг-репорт.";

            }

            bot.getApi().sendMessage(message->chat->id, welcome, false, 0, keyboard);
            waitingForReport[message->chat->id] = false;
            });

        // Ограничение — команда доступна только в чате поддержки
        bot.getEvents().onCommand("reports", [&bot, &db](TgBot::Message::Ptr message) {
            if (message->chat->id != SUPPORT_CHAT_ID) {
                bot.getApi().sendMessage(message->chat->id, u8"эта команда доступна лишь для чата поддержки");
                return;
            }

            try {
                // Получение всех открытых репортов из базы
                auto reports = db.get_open_reports();
                if (reports.empty()) {
                    bot.getApi().sendMessage(message->chat->id, u8"Репортов нету! Продолжайте свою работу");
                    return;
                }
                // Формирование сообщения со списком репортов
                std::string response = u8"📋 Список неотвеченных репортов:\n\n";
                for (const auto& report : reports) {
                    response += u8"🔹 #" + std::to_string(report.id) +
                        u8" от @" + report.user_name +
                        u8"\n📄 Сообщение: " + report.message +
                        u8"\n\n";
                }

                response += u8"\nчтобы ответить на репорт, используй команду /reply [номер]";

                bot.getApi().sendMessage(message->chat->id, response);
            }
            // В случае ошибки выводим её текст
            catch (const std::exception& e) {
                bot.getApi().sendMessage(message->chat->id, std::string(e.what()));
            }
            });


        bot.getEvents().onCommand("reply", [&bot, &db, &waitingForReportResponse](TgBot::Message::Ptr message) {
            if (message->chat->id != SUPPORT_CHAT_ID) {
                bot.getApi().sendMessage(message->chat->id, u8"эта команда доступна лишь для чата поддержки");
                return;
            }

            std::string text = message->text;
            size_t space_pos = text.find(' ');
            if (space_pos == std::string::npos) {
                bot.getApi().sendMessage(message->chat->id, u8"/reply [номер_репорта] [ответ]");
                return;
            }

            try {
                // Извлекаем ID и текст ответа
                int report_id = std::stoi(text.substr(space_pos + 1));
                std::string response = text.substr(text.find(' ', space_pos + 1) + 1);

                if (response.empty()) {
                    bot.getApi().sendMessage(message->chat->id, u8" укажите текст ответа после номера репорта.");
                    return;
                }

                BugReport report = db.get_report(report_id);
                if (report.status != "open") {
                    bot.getApi().sendMessage(message->chat->id, u8"Репорт уже обработан саппортом");
                    return;
                }
                // Обновление в БД и отправка ответа пользователю
                db.update_report_response(report_id, response);
                bot.getApi().sendMessage(report.user_id,
                    u8"Ответ на ваш репорт #" + std::to_string(report_id) + u8":\n" + response);
                bot.getApi().sendMessage(message->chat->id, u8"Ответ на репорт #" + std::to_string(report_id) + u8" отправлен.");
            }
            catch (const std::exception& e) {
                bot.getApi().sendMessage(message->chat->id, u8"Ошибка: " + std::string(e.what()));
            }
            });

        bot.getEvents().onAnyMessage([&bot, &db, &waitingForReport, &waitingForReportResponse](TgBot::Message::Ptr message) {
            if (message->text.empty() || StringTools::startsWith(message->text, "/")) {
                return;
            }
            // Игнорируем команды и пустые сообщения
             // Начать новый репорт
            if (message->text == u8"Отправить сообщение в поддержку") {
                waitingForReport[message->chat->id] = true;
                bot.getApi().sendMessage(message->chat->id, u8"Опишите проблему, которую вы обнаружили ✏️: ");
                return;
            }
            // Пользователь в процессе ввода репорта
            if (waitingForReport[message->chat->id]) {
                waitingForReport[message->chat->id] = false;

                BugReport report;
                report.user_id = message->chat->id;
                report.user_name = message->from->username;
                report.message = message->text;
                report.status = "open";
                //формирование репорта в чат саппортов
                std::string report_msg = u8"📝 New баг-репорт\n"
                    u8"От пользователя @" + report.user_name +
                    u8"\n📄 Текст: \n" + report.message;

                try {
                    // Отправка в чат поддержки
                    auto support_msg = bot.getApi().sendMessage(SUPPORT_CHAT_ID, report_msg);
                    report.message_id_in_support = support_msg->messageId;

                    int report_id = db.add_report(report);

                    bot.getApi().sendMessage(
                        message->chat->id,
                        //Обратная связь
                        u8" ✔️ Accept \n Спасибо за вашу жалобу! Мы рассмотрим его как можно скорее! ID вашего репорта: #" +
                        std::to_string(report_id)
                    );
                }
                catch (const std::exception& e) {
                    bot.getApi().sendMessage(
                        message->chat->id,
                        u8"Ошибка при отправке репорта: " + std::string(e.what())
                    );
                }
                return;
            }

            if (message->chat->id == SUPPORT_CHAT_ID && message->replyToMessage) {
                try {

                    auto open_reports = db.get_open_reports();

                    for (const auto& report : open_reports) {
                        if (report.message_id_in_support == message->replyToMessage->messageId) {
                            db.update_report_response(report.id, message->text);
                            // Обработка ответа саппорта на сообщение через reply
                            std::string reply = u8"Ответ на ваш репорт #" + std::to_string(report.id) +
                                u8":\n" + message->text;
                            bot.getApi().sendMessage(report.user_id, reply);
                            bot.getApi().sendMessage(SUPPORT_CHAT_ID,
                                u8"Ответ на репорт #" + std::to_string(report.id) + u8" отправлен.");
                            return;
                        }
                    }
                }
                catch (const std::exception& e) {
                    bot.getApi().sendMessage(
                        SUPPORT_CHAT_ID,
                        u8"Ошибка при обработке ответа: " + std::string(e.what())
                    );
                }
            }
            });
        //Запуск бесконечного цикла ожидания новых событий
        printf("Bot username: %s\n", bot.getApi().getMe()->username.c_str());
        TgBot::TgLongPoll longPoll(bot);
        while (true) {
            printf("the log was received\n");
            longPoll.start();
        }
    }
    catch (const std::exception& e) {
        printf("Error: %s\n", e.what());
        return 1;
    }
    return 0;
}