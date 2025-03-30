#pragma comment(lib, "Comctl32.lib")

#include "framework.h"
#include "University.Chat.Server.h"

// Поток-приёмник, который принимает подключения от клиентов через именованный канал
DWORD WINAPI ReceiverThread(LPVOID lpParam)
{
    OVERLAPPED oConnect;         // Структура для асинхронных операций
    LPPIPEINST lpPipeInst;         // Указатель на экземпляр структуры, представляющей подключение клиента
    DWORD dwWait, cbRet;
    BOOL fSuccess, fPendingIO;

    // Создаем мьютекс для синхронизации монопольного доступа (эксклюзивного режима)
    hMutex = CreateMutex(NULL, FALSE, L"Global\\ExclusiveAccessMutex");
    if (hMutex == NULL) {
        LogStringCreator(L"Не удалось создать мьютекс. GLE=%d.\n", (const wchar_t*)GetLastError());
    }

    // Создаем событие для асинхронного ожидания подключения клиента
    oConnect.hEvent = CreateEvent(NULL,    // Используем стандартные атрибуты безопасности
        TRUE,    // Ручной сброс события
        TRUE,    // Изначальное состояние – сигнализированное
        NULL);   // Без имени
    if (oConnect.hEvent == NULL)
    {
        LogStringCreator(L"Не удалось создать ивент. GLE=%d.\n", (const wchar_t*)GetLastError());
        return 0;
    }

    // Создаем первый экземпляр именованного канала и начинаем асинхронное ожидание подключения клиента
    fPendingIO = CreateAndConnectInstance(&oConnect);

    // Основной цикл ожидания подключений, пока сервер не завершает работу
    while (!isServerDown) {

        // Ожидаем завершения асинхронной операции или ее прерывания
        dwWait = WaitForSingleObjectEx(oConnect.hEvent, INFINITE, TRUE);
        switch (dwWait) {
        case 0: // Событие сигнализировано
            if (fPendingIO)
            {
                // Проверяем результат асинхронной операции (подключения клиента)
                fSuccess = GetOverlappedResult(hPipe, &oConnect, &cbRet, FALSE);
                if (!fSuccess)
                {
                    LogStringCreator(L"Не удалось подсоединиться. GLE=%d.\n", (const wchar_t*)GetLastError());
                    return 0;
                }
            }
            // Выделяем память для нового экземпляра канала, который будет обрабатывать подключенного клиента
            lpPipeInst = (LPPIPEINST)GlobalAlloc(GPTR, sizeof(PIPEINST));
            if (lpPipeInst == NULL)
            {
                LogStringCreator(L"Не удалось выделить память в куче. GLE=%d.\n", (const wchar_t*)GetLastError());
                return 0;
            }

            // Инициализируем поля структуры экземпляра канала
            lpPipeInst->hPipeInst = hPipe;
            lpPipeInst->cbToWrite = 0;
            // Запускаем цикл чтения/записи для данного клиента с помощью completion routine
            CompletedWriteRoutine(0, 0, (LPOVERLAPPED)lpPipeInst);

            // Создаем новый экземпляр именованного канала для приема следующего клиента
            fPendingIO = CreateAndConnectInstance(&oConnect);
            break;
        case WAIT_IO_COMPLETION:
            // Асинхронная операция завершилась через вызов completion routine, ничего не делаем
            break;
        default:
        {
            LogStringCreator(L"Ожидание ивента канала неожиданно завершилось. GLE=%d.\n", (const wchar_t*)GetLastError());
            return 0;
        }
        }
    }
    return 0;
}

// Функция создания и подключения нового экземпляра именованного канала
BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap)
{
    LPCWSTR lpszPipename = L"\\\\.\\pipe\\MyNamedPipe";

    // Создаем именованный канал с асинхронным режимом (OVERLAPPED)
    hPipe = CreateNamedPipe(
        lpszPipename,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, // Дуплексный доступ, OVERLAPPED I/O
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, // Работа с сообщениями, блокирующий режим ожидания
        PIPE_UNLIMITED_INSTANCES, // Неограниченное количество экземпляров канала
        BUFSIZE * sizeof(TCHAR),
        BUFSIZE * sizeof(TCHAR),
        0,
        NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        LogStringCreator(L"Создание именованного канала неожиданно завершилось. GLE=%d.\n", (const wchar_t*)GetLastError());
        return 0;
    }
    // Пытаемся подключить клиента к каналу
    return ConnectToNewClient(hPipe, lpoOverlap);
}

// Функция, вызываемая после завершения операции записи для запуска операции чтения
void WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap)
{
    LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
    BOOL fRead = FALSE;

    // Если запись прошла успешно, запускаем асинхронное чтение
    if ((dwErr == 0) && (cbWritten == lpPipeInst->cbToWrite))
        fRead = ReadFileEx(
            lpPipeInst->hPipeInst,
            lpPipeInst->chRequest,
            BUFSIZE * sizeof(TCHAR),
            (LPOVERLAPPED)lpPipeInst,
            (LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
    else // Если произошла ошибка или клиент отключился
        LogStringCreator(L"Ошибка или клиент отключился: %d\n", (const wchar_t*)GetLastError());
    // Если не удалось запустить чтение – отключаем клиента и очищаем ресурсы
    if (!fRead)
        DisconnectAndClose(lpPipeInst);
}

// Функция, вызываемая после завершения операции чтения данных от клиента
void WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
    LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
    BOOL fWrite = FALSE;

    // Если чтение прошло успешно и получены данные
    if ((dwErr == 0) && (cbBytesRead != 0))
    {
        GetAnswerToRequest(lpPipeInst);// Обработка запроса клиента

        // Запуск асинхронной операции записи ответа клиенту
        fWrite = WriteFileEx(
            lpPipeInst->hPipeInst,
            lpPipeInst->chReply,
            lpPipeInst->cbToWrite,
            (LPOVERLAPPED)lpPipeInst,
            (LPOVERLAPPED_COMPLETION_ROUTINE)CompletedWriteRoutine);
    }
    else // Если произошла ошибка или клиент закрыл соединение
        LogStringCreator(L"Ошибка или клиент отключился: %d\n", (const wchar_t*)GetLastError());
    // Если не удалось запустить операцию записи – отключаем клиента
    if (!fWrite)
        DisconnectAndClose(lpPipeInst);
}

// Функция обработки запроса от клиента и формирования ответа
void GetAnswerToRequest(LPPIPEINST lpPipeInst) {
    // Определяем, хочет ли клиент эксклюзивный доступ (монопольный режим)
    std::wstring requestStr(lpPipeInst->chRequest);
    bool wantsExclusive = (requestStr.find(L"EXCLUSIVE") != std::wstring::npos);

    EnterCriticalSection(&cs);
    if (wantsExclusive == true) { // Если клиент запросил эксклюзивный режим
        lpPipeInst->bExclusiveMode = true;
        exclusiveActive = true;
        // Если мьютекс еще не захвачен, захватываем его
        if (!mutexRaised) {
            WaitForSingleObject(hMutex, INFINITE);
            mutexRaised = true;
        }
        // Формируем ответ, подтверждающий, что клиент имеет доступ (теперь сервер отправляет ACCESS)
        swprintf_s(lpPipeInst->chReply, BUFSIZE, L"ACCESS: \"%s\"", lpPipeInst->chRequest);
    }
    else if (lpPipeInst->bExclusiveMode == true) { // Клиент ранее находился в эксклюзивном режиме, но теперь его запрос изменился
        lpPipeInst->bExclusiveMode = false;
        exclusiveActive = false;
        // Освобождаем мьютекс, так как клиент больше не требует эксклюзивного доступа
        if (mutexRaised) {
            ReleaseMutex(hMutex);
            mutexRaised = false;
        }
        swprintf_s(lpPipeInst->chReply, BUFSIZE, L"NOEXCLUSIVE");
    }
    else if (exclusiveActive)
        // Если сервер уже находится в эксклюзивном режиме, но клиент не запрашивает его, отвечаем, что доступ запрещен
        swprintf_s(lpPipeInst->chReply, BUFSIZE, L"NOACCESS");
    else
        // Обычный ответ для клиентов, не требующих эксклюзивного доступа
        swprintf_s(lpPipeInst->chReply, BUFSIZE, L"%s", lpPipeInst->chRequest);
    LeaveCriticalSection(&cs);

    // Логируем эхо-запрос
    wchar_t msg[512];
    swprintf_s(msg, L"Ехо клиента: \"%s\"\n", lpPipeInst->chRequest);
    PrintMessage(msg, hEditMsg);
    // Вычисляем размер ответа (в байтах)
    lpPipeInst->cbToWrite = (lstrlen(lpPipeInst->chReply) + 1) * sizeof(TCHAR);
}

// Функция отключения клиента и освобождения ресурсов
void DisconnectAndClose(LPPIPEINST lpPipeInst)
{
    // Если клиент находился в эксклюзивном режиме, освобождаем мьютекс
    if (lpPipeInst->bExclusiveMode) {
        PrintMessage(L"Клиент отключился, освобождаем эксклюзивный режим\r\n", hEditInfo);
        exclusiveActive = false;
        ReleaseMutex(hMutex); // Освобождаем монопольный доступ
    }
    // Отключаем клиентский канал
    if (!DisconnectNamedPipe(lpPipeInst->hPipeInst))
        LogStringCreator(L"DisconnectNamedPipe failed with %d.\r\n", (const wchar_t*)GetLastError());

    CloseHandle(lpPipeInst->hPipeInst);// Закрываем дескриптор канала

    if (lpPipeInst != NULL) {
        GlobalFree(lpPipeInst);// Освобождаем память, выделенную для структуры клиента
    }
}

// Функция подключения клиента к серверу через именованный канал
BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo)
{
    BOOL fConnected, fPendingIO = FALSE;

    fConnected = ConnectNamedPipe(hPipe, lpo);// Пытаемся подключить клиента асинхронно
    if (fConnected)
    {
        LogStringCreator(L"ConnectNamedPipe failed with %d.\n", (const wchar_t*)GetLastError());
        return 0;
    }
    // Обрабатываем возможные коды ошибок
    switch (GetLastError())
    {

    case ERROR_IO_PENDING: // Соединение в процессе, ожидаем завершения операции
        fPendingIO = TRUE;
        break;
    case ERROR_PIPE_CONNECTED:// Клиент уже подключен, сигнализируем об этом
        if (SetEvent(lpo->hEvent))
            break;
    default:// Если произошла другая ошибка – логируем и возвращаем неудачу
    {
        LogStringCreator(L"ConnectNamedPipe failed with %d.\n", (const wchar_t*)GetLastError());
        return 0;
    }
    }
    return fPendingIO;
}

// Точка входа сервера, стандартная WinMain функция
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Инициализируем критическую секцию для синхронизации
    InitializeCriticalSection(&cs);

    // Инициализация глобальных строк и регистрация оконного класса
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_UNIVERSITYCHATSERVER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Выполняем инициализацию приложения (создаем главное окно)
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_UNIVERSITYCHATSERVER));
    MSG msg;

    // Основной цикл обработки сообщений
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    // Удаляем критическую секцию перед завершением
    DeleteCriticalSection(&cs);

    return (int)msg.wParam;
}

// Регистрация оконного класса
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_UNIVERSITYCHATSERVER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_UNIVERSITYCHATSERVER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// Инициализация приложения (создание главного окна)
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowEx(0, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_X, WINDOW_Y, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// Основная оконная процедура, обрабатывает сообщения окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_START:
        {
            OnStartServer(hWnd);
            break;
        }
        case IDC_BTN_STOP:
        {
            OnStopServer();
            break;
        }
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDC_BTN_EXIT:
        case IDM_EXIT:
            if (MessageBoxExW(hWnd, L"Вы уверены, что хотите выйти?", L"Подтвердите", MB_YESNO | MB_ICONQUESTION, NULL) == IDYES)
            {
                DestroyWindow(hWnd);
            }
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        EndPaint(hWnd, &ps);
        break;
    }
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* pmmi = (MINMAXINFO*)lParam;
        pmmi->ptMinTrackSize.x = WINDOW_X;
        pmmi->ptMinTrackSize.y = WINDOW_Y;
        break;
    }
    case WM_CREATE: {
        OnCreate(hWnd);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Диалоговое окно "О программе"
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Инициализация элементов управления окна
void OnCreate(HWND hWnd)
{
    InitCommonControls();

    // Создаем кнопку для запуска сервера
    hButtonStart = CreateWindow(L"BUTTON", L"Запустить сервер",
        WS_VISIBLE | WS_CHILD | BS_FLAT,
        0, 0, 350, 50, hWnd, (HMENU)IDC_BTN_START, hInst, NULL);

    // Создаем кнопку для остановки сервера (изначально отключена)
    hButtonClose = CreateWindow(L"BUTTON", L"Остановить сервер",
        WS_VISIBLE | WS_CHILD | BS_FLAT | WS_DISABLED,
        350, 0, 350, 50, hWnd, (HMENU)IDC_BTN_STOP, hInst, NULL);

    // Кнопка выхода
    hButtonExit = CreateWindow(L"BUTTON", L"Выход",
        WS_CHILD | WS_VISIBLE,
        700, 0, 80, 50, hWnd, (HMENU)IDC_BTN_EXIT, hInst, NULL);

    // Статические метки для сообщений
    HWND hLabel1 = CreateWindow(L"STATIC", L"Сообщение", WS_CHILD | WS_VISIBLE,
        10, 70, 380, 20, hWnd, (HMENU)301, hInst, NULL);
    HWND hLabel2 = CreateWindow(L"STATIC", L"Информация", WS_CHILD | WS_VISIBLE,
        400, 70, 380, 20, hWnd, (HMENU)302, hInst, NULL);

    // Элемент для вывода отправляемых сообщений (только для чтения)
    hEditMsg = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        10, 90, 380, 300, hWnd, (HMENU)IDC_EDIT_MSG, hInst, NULL);
    if (hEditMsg == NULL) {
        MessageBox(hWnd, L"Не удалось создать элемент управления.", L"Ошибка", MB_ICONERROR);
        return;
    }

    // Элемент для вывода информационных сообщений (только для чтения)
    hEditInfo = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        400, 90, 380, 300, hWnd, (HMENU)IDC_EDIT_INFO, hInst, NULL);
    if (hEditInfo == NULL) {
        MessageBox(hWnd, L"Не удалось создать элемент управления.", L"Ошибка", MB_ICONERROR);
        return;
    }
}

// Функция запуска сервера по нажатию кнопки "Запустить сервер"
void OnStartServer(HWND hWnd)
{
    if (hServerThread == NULL)
    {
        isServerDown = false;
        exclusiveActive = false;

        // Создаем поток сервера
        hServerThread = CreateThread(NULL, 0, ReceiverThread, (LPVOID)hWnd, 0, NULL);
        if (hServerThread)
        {
            PrintMessage(L"Сервер запущен.\r\n", hEditInfo);
            EnableWindow(hButtonStart, FALSE);
            EnableWindow(hButtonClose, TRUE);
        }
        else {
            PrintMessage(L"Не удалось запустить сервер.\r\n", hEditInfo);
        }
    }
}

// Функция остановки сервера по нажатию кнопки "Остановить сервер"
void OnStopServer()
{
    // Флаг завершения работы сервера
    isServerDown = true;
    exclusiveActive = false;

    if (hServerThread != NULL)
    {
        // Ждем завершения потока сервера в течение 3 секунд
        WaitForSingleObject(hServerThread, 3000);
        EnableWindow(hButtonStart, TRUE);
        EnableWindow(hButtonClose, FALSE);
        CloseHandle(hServerThread);
        hServerThread = NULL;
        PrintMessage(L"Сервер остановлен.\r\n", hEditInfo);
    }
    else
        PrintMessage(L"Не удалось остановить сервер\r\n", hEditInfo);
}

// Функция логирования строк (для отладки)
void LogStringCreator(const wchar_t* msg, const wchar_t* payload)
{
    wchar_t buff[256];
    swprintf_s(buff, msg, payload);
    PrintMessage(buff, hEditInfo);
}

// Функция вывода сообщений в окно информационного журнала
void PrintMessage(const wchar_t* msg, HWND window)
{
    if (window != NULL) {
        int length = GetWindowTextLengthW(window);
        SendMessageW(window, EM_SETSEL, (WPARAM)length, (LPARAM)length);
        SendMessageW(window, EM_REPLACESEL, 0, (LPARAM)(GetCurrentTimeString().append(msg).c_str()));
    }
}

// Функция получения строки с текущим временем
std::wstring GetCurrentTimeString()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buffer[64];
    swprintf_s(buffer, 64, L"[%02d:%02d:%02d]: ", st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buffer);
}