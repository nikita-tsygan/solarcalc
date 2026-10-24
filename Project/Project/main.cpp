#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include <cmath>
#include <string>
#include <cstring>
#include <algorithm> // Для std::min

// --- УМНОЕ ПОДКЛЮЧЕНИЕ WINDOWS.H ДЛЯ СОВМЕСТИМОСТИ С RAYLIB И HTMLHELP ---
#define NOGDI // Отключаем ненужные элементы GDI

// Временно переименовываем конфликтующие функции Raylib, 
// чтобы макросы из windows.h не перекрывали их
#define CloseWindow WinAPICloseWindow
#define ShowCursor   WinAPIShowCursor
#define LoadImage    WinAPILoadImage

#include <windows.h>
#include <htmlhelp.h>

// Возвращаем имена функций Raylib обратно в исходное состояние
#undef CloseWindow
#undef ShowCursor
#undef LoadImage
#undef NOGDI

#pragma comment(lib, "htmlhelp.lib")

// Для запуска без отображения консоли в Windows
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

// Константы расчетной модели
const double P_PANEL = 0.4;       // Номинальная мощность одной панели (400 Вт), кВт
const double K_LOSS = 0.82;       // Коэффициент потерь системы (82% эффективности)
const double E_SUN = 3.5;         // Среднесуточное количество пиковых часов солнца для РБ
const double PRICE_ON = 1100.0;   // Стоимость 1 кВт Сетевой СЭС под ключ (BYN)
const double PRICE_OFF = 2600.0;  // Стоимость 1 кВт Автономной СЭС с АКБ под ключ (BYN)
const double TARIFF_GREEN = 0.25; // Зеленый тариф для продажи излишков в сеть (BYN/кВт*ч)

// Управление состояниями приложения
enum Screen { WELCOME, CALCULATOR };
Screen currentScreen = WELCOME;
bool showAboutModal = false;
bool inputError = false;          // Флаг обнаружения ошибки ввода

// Буферы ввода и глобальные переменные состояния
char energyInputBuf[32] = "490";
char tariffInputBuf[16] = "0.3265"; // Буфер для ввода тарифа сети (Постановление СМ РБ №93)
int selectedSystemType = 1;         // 1 = Автономная, 2 = Сетевая

// Выходные переменные для интерфейса
double outDailyUsage = 0.0;
double outPowerPeakCalculated = 0.0;
int outPanelsCount = 0;
double outActualPower = 0.0;
double outTotalCostBYN = 0.0;
double outAnnualSavingsBYN = 0.0;
double outPaybackYears = 0.0;
double outCo2Saved = 0.0;
int outTreesEquivalent = 0;
double outTariffGrid = 0.3265;     // Динамическая переменная тарифа
bool isCalculationDone = false;

// Указатели на шрифты разного размера
ImFont* defaultFont = nullptr;
ImFont* titleFont = nullptr;

// Функция выполнения инженерно-экономических расчетов
static void ExecuteCalculation() {
    using namespace std;

    try {
        // 1. Проверка на пустое поле ввода энергии или тарифа
        if (strlen(energyInputBuf) == 0 || strlen(tariffInputBuf) == 0) {
            inputError = true;
            isCalculationDone = false;
            return;
        }

        // Попытка преобразования строк в числа
        double eMonth = stod(energyInputBuf);
        outTariffGrid = stod(tariffInputBuf);

        // 2. Валидация диапазонов (Энергия: 0.1-50000, Тариф: 0.01-2.0)
        if (eMonth <= 0.1 || eMonth > 50000.0 || outTariffGrid <= 0.01 || outTariffGrid > 2.0) {
            inputError = true;
            isCalculationDone = false;
            return;
        }

        inputError = false;
        bool isOffGrid = (selectedSystemType == 1);

        // --- БЛОК 1: ТЕХНИЧЕСКИЕ ПАРАМЕТРЫ СИСТЕМЫ ---
        outDailyUsage = eMonth / 30.0;
        outPowerPeakCalculated = outDailyUsage / (E_SUN * K_LOSS);
        outPanelsCount = static_cast<int>(ceil(outPowerPeakCalculated / P_PANEL));

        if (outPanelsCount < 1) outPanelsCount = 1;
        outActualPower = outPanelsCount * P_PANEL;

        // --- БЛОК 2: ФИНАНСОВЫЕ РАСЧЕТЫ (СМЕТА) ---
        if (isOffGrid) {
            outTotalCostBYN = outActualPower * PRICE_OFF;
        }
        else {
            outTotalCostBYN = outActualPower * PRICE_ON;
        }

        // --- БЛОК 3: ЭКОНОМИЧЕСКОЕ ПРОГНОЗИРОВАНИЕ ---
        double annualHouseDemand = eMonth * 12.0;
        double annualStationGeneration = outActualPower * E_SUN * 365.0 * K_LOSS;

        if (isOffGrid) {
            double actualUsedEnergy = min(annualStationGeneration, annualHouseDemand);
            outAnnualSavingsBYN = actualUsedEnergy * outTariffGrid;
        }
        else {
            if (annualStationGeneration <= annualHouseDemand) {
                outAnnualSavingsBYN = annualStationGeneration * outTariffGrid;
            }
            else {
                double surplusEnergy = annualStationGeneration - annualHouseDemand;
                outAnnualSavingsBYN = (annualHouseDemand * outTariffGrid) + (surplusEnergy * TARIFF_GREEN);
            }
        }

        // Защищенный расчет срока окупаемости инвестиций
        if (outAnnualSavingsBYN > 0.0) {
            outPaybackYears = outTotalCostBYN / outAnnualSavingsBYN;
        }
        else {
            outPaybackYears = 0.0;
        }

        if (outPaybackYears < 1.0 && outPaybackYears > 0.0) outPaybackYears = 1.0;

        // --- БЛОК 4: ЭКОЛОГИЧЕСКИЙ ЭФФЕКТ ---
        double effectiveEcoEnergy = min(annualStationGeneration, annualHouseDemand);
        outCo2Saved = effectiveEcoEnergy * 0.5;
        outTreesEquivalent = static_cast<int>(ceil(outCo2Saved / 22.0));

        isCalculationDone = true;
    }
    catch (...) {
        inputError = true;
        isCalculationDone = false;
    }
}

// Настройка визуального стиля (Темная бирюзовая тема)
static void ApplyUIStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ChildRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ItemSpacing = ImVec2(12, 12);

    colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.65f, 0.60f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.75f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.55f, 0.50f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.23f, 0.65f, 0.60f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 650, "Экологический калькулятор: солнечные панели");

    // Теперь это железно вызовет именно функцию Raylib
    Image icon = ::LoadImage("app_icon.png");
    SetWindowIcon(icon);
    UnloadImage(icon);

    SetTargetFPS(60);

    rlImGuiSetup(true);
    ApplyUIStyle();

    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* cyrillicRange = io.Fonts->GetGlyphRangesCyrillic();

    defaultFont = io.Fonts->AddFontFromFileTTF("tahoma.ttf", 17.0f, nullptr, cyrillicRange);
    titleFont = io.Fonts->AddFontFromFileTTF("tahoma.ttf", 24.0f, nullptr, cyrillicRange);

    if (defaultFont != nullptr) {
        io.FontDefault = defaultFont;
    }

    ExecuteCalculation();

    // Главный цикл приложения
    while (!WindowShouldClose()) {

        // --- ПЕРЕХВАТ НАЖАТИЯ КЛАВИШИ F1 ДЛЯ ОТКРЫТИЯ СПРАВКИ ---
        if (IsKeyPressed(KEY_F1)) {
            // Передаем дескриптор корректно, исключая неоднозначность типов
            HtmlHelpA(static_cast<HWND>(0), "MyHelp.chm", HH_DISPLAY_TOPIC, 0);
        }

        float currentWidth = (float)GetScreenWidth();
        float currentHeight = (float)GetScreenHeight();

        BeginDrawing();
        ClearBackground(GetColor(0x181A1C));

        rlImGuiBegin();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(currentWidth, currentHeight));
        ImGui::Begin("MasterCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (currentScreen == WELCOME) {
            // --- ЭКРАН ПРИВЕТСТВИЯ ---
            float cardWidth = 660.0f;
            float cardHeight = 260.0f;

            float posX = (currentWidth - cardWidth) / 2.0f;
            float posY = (currentHeight - cardHeight) / 2.0f;

            ImGui::SetNextWindowPos(ImVec2(posX, posY));
            ImGui::BeginChild("WelcomeFrame", ImVec2(cardWidth, cardHeight), true);

            const char* welcomeText = u8"Экологический калькулятор: солнечные панели";
            float textWidth = 0.0f;

            if (titleFont != nullptr) ImGui::PushFont(titleFont);
            textWidth = ImGui::CalcTextSize(welcomeText).x;
            float textPosX = (cardWidth - textWidth) / 2.0f;

            ImGui::SetCursorPos(ImVec2(textPosX, 45));
            ImGui::Text(welcomeText);
            if (titleFont != nullptr) ImGui::PopFont();

            ImGui::SetCursorPos(ImVec2(75, 145));
            if (ImGui::Button(u8"ЗАПУСТИТЬ", ImVec2(230, 45))) {
                currentScreen = CALCULATOR;
                ExecuteCalculation();
            }

            ImGui::SetCursorPos(ImVec2(355, 145));
            if (ImGui::Button(u8"ОБ АВТОРЕ", ImVec2(230, 45))) {
                showAboutModal = true;
            }

            ImGui::EndChild();

            // --- МОДАЛЬНОЕ ОКНО "О РАЗРАБОТЧИКЕ" ---
            if (showAboutModal) {
                ImGui::OpenPopup(u8"О разработчике");
            }

            float modalW = 510.0f;
            float modalH = 320.0f;
            ImGui::SetNextWindowSize(ImVec2(modalW, modalH));
            ImGui::SetNextWindowPos(ImVec2((currentWidth - modalW) / 2.0f, (currentHeight - modalH) / 2.0f));

            if (ImGui::BeginPopupModal(u8"О разработчике", &showAboutModal, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Spacing();
                ImGui::Text(u8"КУРСОВОЙ ПРОЕКТ");
                ImGui::TextDisabled(u8"Тема: Разработка программного продукта\n«Экологический калькулятор: солнечные панели»");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text(u8"Разработчик: Цыган Никита Дмитриевич");
                ImGui::Text(u8"Курс: 2");
                ImGui::Text(u8"Группа: ПЗТ-45");
                ImGui::Spacing();
                ImGui::Separator();

                ImGui::SetCursorPos(ImVec2(modalW - 130, modalH - 55));
                if (ImGui::Button(u8"ЗАКРЫТЬ", ImVec2(100, 35))) {
                    showAboutModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        else if (currentScreen == CALCULATOR) {
            // --- РАБОЧЕЕ ПРОСТРАНСТВО КАЛЬКУЛЯТОРА ---
            ImGui::SetCursorPos(ImVec2(20, 15));
            if (ImGui::Button(u8"< Назад", ImVec2(90, 30))) {
                currentScreen = WELCOME;
                inputError = false;
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(21);
            ImGui::Text(u8"  |    ИНЖЕНЕРНЫЕ И ЭКОНОМИЧЕСКИЕ РАСЧЕТЫ");

            ImGui::SetCursorPos(ImVec2(20, 55));
            ImGui::Separator();

            float workspaceHeight = currentHeight - 80.0f;
            float leftPanelWidth = currentWidth * 0.35f;
            if (leftPanelWidth < 320.0f) leftPanelWidth = 320.0f;

            // ЛЕВАЯ ПАНЕЛЬ: Ввод исходных данных
            ImGui::SetCursorPos(ImVec2(20, 70));
            ImGui::BeginChild("InputPanel", ImVec2(leftPanelWidth, workspaceHeight - 10.0f), true);
            ImGui::Spacing();

            ImGui::Text(u8"Месячное потребление дома (кВт*ч):");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##EnergyInput", energyInputBuf, IM_ARRAYSIZE(energyInputBuf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                ExecuteCalculation();
            }

            ImGui::Spacing(); ImGui::Spacing();

            ImGui::Text(u8"Тариф сети за 1 кВт*ч (BYN):");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##TariffInput", tariffInputBuf, IM_ARRAYSIZE(tariffInputBuf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                ExecuteCalculation();
            }

            if (inputError) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"Ошибка: Проверьте диапазоны ввода!");
            }
            else {
                ImGui::Spacing();
            }

            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Text(u8"Архитектура солнечной станции:");

            if (ImGui::RadioButton(u8"Автономная", &selectedSystemType, 1)) {
                ExecuteCalculation();
            }
            if (ImGui::RadioButton(u8"Сетевая", &selectedSystemType, 2)) {
                ExecuteCalculation();
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 55);
            if (ImGui::Button(u8"ВЫПОЛНИТЬ РАСЧЕТ", ImVec2(-1, 40))) {
                ExecuteCalculation();
            }
            ImGui::EndChild();

            // ПРАВАЯ ПАНЕЛЬ: Вывод результатов расчетов по карточкам
            float rightPanelX = leftPanelWidth + 40.0f;
            float rightPanelWidth = currentWidth - rightPanelX - 20.0f;

            ImGui::SetCursorPos(ImVec2(rightPanelX, 70));
            ImGui::BeginChild("OutputPanel", ImVec2(rightPanelWidth, workspaceHeight - 10.0f), false);

            if (!isCalculationDone) {
                ImGui::SetCursorPos(ImVec2(30, 30));
                ImGui::TextDisabled(u8">> Ожидание корректного ввода целевых параметров...");
            }
            else {
                float cardH = (ImGui::GetWindowHeight() - 36.0f) / 4.0f;

                // КАРТОЧКА 1: Технические параметры генерации
                ImGui::BeginChild("TechCard", ImVec2(-1, cardH), true);
                ImGui::TextColored(ImVec4(0.23f, 0.65f, 0.60f, 1.0f), u8"[Технические параметры системы]");
                ImGui::Text(u8"Среднесуточный расход объекта: %.2f кВт*ч/день", outDailyUsage);
                ImGui::Text(u8"Расчетная пиковая мощность СЭС: %.2f кВт", outPowerPeakCalculated);
                ImGui::Text(u8"Фактическая мощность панелей: %.2f кВт (Установка: %d модулей по 400 Вт)", outActualPower, outPanelsCount);
                ImGui::EndChild();

                // КАРТОЧКА 2: Финансовые затраты (Смета)
                ImGui::BeginChild("FinanceCard", ImVec2(-1, cardH), true);
                ImGui::TextColored(ImVec4(0.23f, 0.65f, 0.60f, 1.0f), u8"[Финансовые бюджетные оценки]");
                ImGui::Text(u8"Общая стоимость станции под ключ: %.0f BYN", outTotalCostBYN);
                ImGui::EndChild();

                // КАРТОЧКА 3: Экономические показатели возврата инвестиций
                ImGui::BeginChild("EconomyCard", ImVec2(-1, cardH), true);
                ImGui::TextColored(ImVec4(0.23f, 0.65f, 0.60f, 1.0f), u8"[Экономическое прогнозирование]");
                ImGui::Text(u8"Чистая денежная экономия в год: %.2f BYN/год (Расчетный тариф: %.4f BYN)", outAnnualSavingsBYN, outTariffGrid);
                if (selectedSystemType == 2) {
                    ImGui::Text(u8"Срок окупаемости СЭС: %.1f лет (С учетом выкупа излишков сетью)", outPaybackYears);
                }
                else {
                    ImGui::Text(u8"Срок окупаемости СЭС: %.1f лет (Ограничено ресурсом циклов АКБ)", outPaybackYears);
                }
                ImGui::EndChild();

                // КАРТОЧКА 4: Экологический мониторинг
                ImGui::BeginChild("EcoCard", ImVec2(-1, cardH), true);
                ImGui::TextColored(ImVec4(0.23f, 0.65f, 0.60f, 1.0f), u8"[Экологический эффект проекта]");
                ImGui::Text(u8"Предотвращенный углеродный след (CO2): %.0f кг/год", outCo2Saved);
                ImGui::Text(u8"Эквивалент высаженного леса: %d деревьев", outTreesEquivalent);
                ImGui::EndChild();
            }

            ImGui::EndChild();
        }

        ImGui::End();
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();

    return 0;
}