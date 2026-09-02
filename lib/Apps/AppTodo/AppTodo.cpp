#include "AppTodo.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_todo.h"
#include "../Book32_Core/Book32FS.h"
#include <ArduinoJson.h>

const uint8_t* AppTodo::getIconImage() { return icon_todo_160x160; }

struct TodoDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static TodoDirtyRect todoItemRect(int index, int screenW) {
    const int BACK_Y = 55;
    const int BACK_HEIGHT = 50;
    const int ITEM_HEIGHT = 70;
    if (index < 0) {
        return {0, BACK_Y, screenW, BACK_HEIGHT + 4};
    }
    return {0, BACK_Y + BACK_HEIGHT + (index * ITEM_HEIGHT), screenW, ITEM_HEIGHT + 4};
}

static TodoDirtyRect unionTodoRect(TodoDirtyRect a, TodoDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

void AppTodo::start() {
    _needsRedraw = true;
    _firstDraw = true;
    _selectedIndex = -1;
    _previousSelectedIndex = -1;
    _selectionOnlyRedraw = false;
    loadTodos();
    InputMgr::getInstance().setCallback(std::bind(&AppTodo::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppTodo::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));
}

void AppTodo::stop() {
    saveTodos();
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppTodo::loadTodos() {
    _todos.clear();
    _nextId = 1;

    File file;
    if (EbookFS.exists("/todos.json")) {
        file = EbookFS.open("/todos.json", "r");
    } else if (SystemFS.exists("/todos.json")) {
        file = SystemFS.open("/todos.json", "r");
    }

    if (file) {
        DynamicJsonDocument doc(4096);
        if (!deserializeJson(doc, file)) {
            JsonArray arr = doc["todos"].as<JsonArray>();
            for (JsonObject obj : arr) {
                TodoItem item;
                item.id = obj["id"] | _nextId;
                item.text = obj["text"].as<String>();
                item.completed = obj["completed"] | false;
                _todos.push_back(item);
                if (item.id >= _nextId) _nextId = item.id + 1;
            }
        }
        file.close();
    }
    Serial.printf("Loaded %d todos\n", _todos.size());
}

void AppTodo::saveTodos() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("todos");

    for (const auto& item : _todos) {
        JsonObject obj = arr.createNestedObject();
        obj["id"] = item.id;
        obj["text"] = item.text;
        obj["completed"] = item.completed;
    }

    File file = EbookFS.open("/todos.json", FILE_WRITE);
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("Saved todos");
    }
}

void AppTodo::addTodo(const String& text) {
    if (text.length() == 0) return;
    TodoItem item;
    item.id = _nextId++;
    item.text = text;
    item.completed = false;
    _todos.push_back(item);
    saveTodos();
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
    Serial.printf("Added todo: %s\n", text.c_str());
}

void AppTodo::toggleTodo(int id) {
    for (auto& item : _todos) {
        if (item.id == id) {
            item.completed = !item.completed;
            saveTodos();
            _selectionOnlyRedraw = false;
            _needsRedraw = true;
            Serial.printf("Toggled todo %d: %s\n", id, item.completed ? "done" : "pending");
            return;
        }
    }
}

void AppTodo::editTodo(int id, const String& text) {
    for (auto& item : _todos) {
        if (item.id == id) {
            item.text = text;
            saveTodos();
            _selectionOnlyRedraw = false;
            _needsRedraw = true;
            Serial.printf("Edited todo %d: %s\n", id, text.c_str());
            return;
        }
    }
}

void AppTodo::deleteTodo(int id) {
    for (auto it = _todos.begin(); it != _todos.end(); ++it) {
        if (it->id == id) {
            Serial.printf("Deleted todo %d\n", id);
            _todos.erase(it);
            saveTodos();
            _selectionOnlyRedraw = false;
            _needsRedraw = true;
            return;
        }
    }
}

void AppTodo::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

    int maxIndex = (int)_todos.size() - 1;

    if (action == INPUT_NEXT) {
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex++;
        if (_selectedIndex > maxIndex) _selectedIndex = -1;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
    else if (action == INPUT_PREV) {
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex--;
        if (_selectedIndex < -1) _selectedIndex = maxIndex;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (_selectedIndex == -1) {
            // Back to main menu
            AppMgr::getInstance().switchTo(0);
        } else if (_selectedIndex >= 0 && _selectedIndex < (int)_todos.size()) {
            // Toggle the selected todo
            toggleTodo(_todos[_selectedIndex].id);
        }
    }
}

void AppTodo::handleTouch(uint16_t, uint16_t y) {
    constexpr int BACK_Y = 55;
    constexpr int BACK_HEIGHT = 50;
    constexpr int ITEM_HEIGHT = 70;

    if (y >= BACK_Y && y < BACK_Y + BACK_HEIGHT) {
        AppMgr::getInstance().switchTo(0);
        return;
    }

    if (y < BACK_Y + BACK_HEIGHT) return;
    int index = (static_cast<int>(y) - BACK_Y - BACK_HEIGHT) / ITEM_HEIGHT;
    if (index >= 0 && index < static_cast<int>(_todos.size())) {
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex = index;
        toggleTodo(_todos[index].id);
    }
}

void AppTodo::update() {}

void AppTodo::forceRedraw() {
    _firstDraw = true;  // Full repaint at the new orientation
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppTodo::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    drawTodoList();
}

void AppTodo::drawTodoList() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int16_t screenW = display.width();
    int16_t screenH = display.height();

    // Use partial refresh after first draw
    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        TodoDirtyRect dirty = unionTodoRect(todoItemRect(_previousSelectedIndex, screenW),
                                           todoItemRect(_selectedIndex, screenW));
        TodoDirtyRect footer = {0, screenH - 46, screenW, 46};
        dirty = unionTodoRect(dirty, footer);
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }
    _selectionOnlyRedraw = false;

    const int BACK_HEIGHT = 50;
    const int ITEM_HEIGHT = 70;  // Increased for 2-line text
    const int CHECKBOX_SIZE = 24;
    const int PADDING = 20;
    const int TEXT_X = PADDING + CHECKBOX_SIZE + 15;
    const int MAX_TEXT_WIDTH = screenW - TEXT_X - PADDING - 10;  // Available width for text

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header
        fontMgr.drawText(display, "Todo List", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(0, 50, screenW, 50, GxEPD_BLACK);

        int y = 55;

        // Back to Menu option
        bool backSelected = (_selectedIndex == -1);
        if (backSelected) {
            display.fillRect(0, y, screenW, BACK_HEIGHT, GxEPD_BLACK);
        }
        uint16_t backColor = backSelected ? GxEPD_WHITE : GxEPD_BLACK;
        fontMgr.drawText(display, "<  Back to Menu", PADDING, y + 32, FONT_SIZE_MENU, backColor);
        if (!backSelected) {
            display.drawLine(PADDING, y + BACK_HEIGHT - 2, screenW - PADDING, y + BACK_HEIGHT - 2, GxEPD_BLACK);
        }
        y += BACK_HEIGHT;

        // Todo items
        if (_todos.empty()) {
            fontMgr.drawText(display, "No tasks yet.", PADDING, y + 30, FONT_SIZE_BODY, GxEPD_BLACK);
            fontMgr.drawText(display, "Add tasks via web interface.", PADDING, y + 55, FONT_SIZE_SMALL, GxEPD_BLACK);
        } else {
            int idx = 0;
            for (const auto& item : _todos) {
                if (y > screenH - 80) break;

                bool isSelected = (idx == _selectedIndex);
                if (isSelected) {
                    display.fillRect(0, y, screenW, ITEM_HEIGHT, GxEPD_BLACK);
                }

                uint16_t textColor = isSelected ? GxEPD_WHITE : GxEPD_BLACK;
                uint16_t boxColor = isSelected ? GxEPD_WHITE : GxEPD_BLACK;

                // Draw checkbox
                int cbX = PADDING;
                int cbY = y + (ITEM_HEIGHT - CHECKBOX_SIZE) / 2;
                display.drawRect(cbX, cbY, CHECKBOX_SIZE, CHECKBOX_SIZE, boxColor);

                // Draw checkmark if completed
                if (item.completed) {
                    // Draw X or checkmark inside
                    display.drawLine(cbX + 4, cbY + 4, cbX + CHECKBOX_SIZE - 4, cbY + CHECKBOX_SIZE - 4, boxColor);
                    display.drawLine(cbX + CHECKBOX_SIZE - 4, cbY + 4, cbX + 4, cbY + CHECKBOX_SIZE - 4, boxColor);
                }

                // Draw task text with word wrap (up to 2 lines)
                String text = item.text;
                String line1 = "";
                String line2 = "";

                // Split text into words and wrap
                int textWidth = fontMgr.getTextWidth(text.c_str(), FONT_SIZE_MENU);
                if (textWidth <= MAX_TEXT_WIDTH) {
                    // Fits on one line
                    line1 = text;
                } else {
                    // Need to wrap - find where to break
                    String currentLine = "";
                    int lastSpace = -1;
                    for (unsigned int i = 0; i < text.length(); i++) {
                        char c = text.charAt(i);
                        String testLine = currentLine + c;
                        int testWidth = fontMgr.getTextWidth(testLine.c_str(), FONT_SIZE_MENU);

                        if (c == ' ') lastSpace = currentLine.length();

                        if (testWidth > MAX_TEXT_WIDTH) {
                            // Line is too long, break at last space or here
                            if (lastSpace > 0 && line1.length() == 0) {
                                line1 = currentLine.substring(0, lastSpace);
                                currentLine = currentLine.substring(lastSpace + 1) + c;
                            } else if (line1.length() == 0) {
                                line1 = currentLine;
                                currentLine = String(c);
                            } else {
                                break;  // Already have line1, stop
                            }
                            lastSpace = -1;
                        } else {
                            currentLine += c;
                        }
                    }
                    if (line1.length() == 0) {
                        line1 = currentLine;
                    } else {
                        line2 = currentLine;
                        // Truncate line2 if still too long
                        while (line2.length() > 0 && fontMgr.getTextWidth(line2.c_str(), FONT_SIZE_MENU) > MAX_TEXT_WIDTH) {
                            line2 = line2.substring(0, line2.length() - 4) + "...";
                        }
                    }
                }

                int textY1 = y + (line2.length() > 0 ? 25 : (ITEM_HEIGHT / 2) + 6);
                int textY2 = y + 50;

                fontMgr.drawText(display, line1.c_str(), TEXT_X, textY1, FONT_SIZE_MENU, textColor);
                if (line2.length() > 0) {
                    fontMgr.drawText(display, line2.c_str(), TEXT_X, textY2, FONT_SIZE_MENU, textColor);
                }

                // Strikethrough for completed items
                if (item.completed) {
                    int line1Width = fontMgr.getTextWidth(line1.c_str(), FONT_SIZE_MENU);
                    display.drawLine(TEXT_X, textY1 - 6, TEXT_X + line1Width, textY1 - 6, textColor);
                    if (line2.length() > 0) {
                        int line2Width = fontMgr.getTextWidth(line2.c_str(), FONT_SIZE_MENU);
                        display.drawLine(TEXT_X, textY2 - 6, TEXT_X + line2Width, textY2 - 6, textColor);
                    }
                }

                if (!isSelected) {
                    display.drawLine(PADDING, y + ITEM_HEIGHT - 2, screenW - PADDING, y + ITEM_HEIGHT - 2, GxEPD_BLACK);
                }

                y += ITEM_HEIGHT;
                idx++;
            }
        }

        // Navigation help at bottom
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Toggle/Select", screenH - 25, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}
