/**
 * @file auth.c
 * @brief Browser-based employee authentication portal implementation.
 */

#include "auth.h"
#include "protocol.h"
#include <ctype.h>
#include <time.h>

static const auth_employee_t g_default_employees[] = {
    {"EMP001", "alice123", "Alice", "Engineering"},
    {"EMP002", "bob123", "Bob", "Security Operations"},
    {"EMP003", "charlie123", "Charlie", "Systems Architecture"},
    {"EMP101", "osproject2026", "Rey", "Kernel Development"},
    {"EMP102", "adminpass", "Administrator", "IT Infrastructure"}
};
static const size_t g_default_employee_count = sizeof(g_default_employees) / sizeof(g_default_employees[0]);

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t d = 0;
    while (*src && d + 1 < dst_size) {
        if (*src == '+') {
            dst[d++] = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            dst[d++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[d++] = *src++;
        }
    }
    dst[d] = '\0';
}

static void trim_whitespace(char *str)
{
    if (!str) return;
    char *p = str;
    while (isspace((unsigned char)*p)) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);

    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

int auth_verify_credentials(const char *emp_id, const char *password, auth_employee_t *out_emp)
{
    if (!emp_id || !password) return -1;

    char clean_id[MAX_EMP_ID_LEN];
    char clean_pass[MAX_EMP_PASS_LEN];
    snprintf(clean_id, sizeof(clean_id), "%s", emp_id);
    snprintf(clean_pass, sizeof(clean_pass), "%s", password);
    trim_whitespace(clean_id);
    trim_whitespace(clean_pass);

    if (strlen(clean_id) == 0 || strlen(clean_pass) == 0) return -1;

    /* 1. Try reading registry from file */
    const char *paths[] = {
        "storage/employees.txt",
        "../storage/employees.txt",
        "employees.txt"
    };

    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        fp = fopen(paths[i], "r");
        if (fp) break;
    }

    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            proto_strip_newline(line);
            trim_whitespace(line);
            if (line[0] == '#' || strlen(line) == 0) continue;

            char f_id[MAX_EMP_ID_LEN] = {0};
            char f_pass[MAX_EMP_PASS_LEN] = {0};
            char f_name[MAX_EMP_NAME_LEN] = {0};
            char f_dept[MAX_EMP_DEPT_LEN] = {0};

            /* Format: id:pass:name:dept */
            if (sscanf(line, "%31[^:]:%63[^:]:%63[^:]:%63[^\n]", f_id, f_pass, f_name, f_dept) >= 2) {
                if (strcasecmp(clean_id, f_id) == 0 && strcmp(clean_pass, f_pass) == 0) {
                    fclose(fp);
                    if (out_emp) {
                        snprintf(out_emp->employee_id, sizeof(out_emp->employee_id), "%s", f_id);
                        snprintf(out_emp->password, sizeof(out_emp->password), "%s", f_pass);
                        snprintf(out_emp->name, sizeof(out_emp->name), "%s", (strlen(f_name) > 0) ? f_name : f_id);
                        snprintf(out_emp->department, sizeof(out_emp->department), "%s", (strlen(f_dept) > 0) ? f_dept : "Operations");
                    }
                    return 0;
                }
            }
        }
        fclose(fp);
    }

    /* 2. Fall back to compiled default directory */
    for (size_t i = 0; i < g_default_employee_count; i++) {
        if (strcasecmp(clean_id, g_default_employees[i].employee_id) == 0 &&
            strcmp(clean_pass, g_default_employees[i].password) == 0) {
            if (out_emp) {
                *out_emp = g_default_employees[i];
            }
            return 0;
        }
    }

    return -1;
}

static void open_browser_process(const char *url)
{
    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /* Child process: disconnect from terminal output to avoid clutter */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        execlp("sensible-browser", "sensible-browser", url, (char *)NULL);
        execlp("google-chrome", "google-chrome", url, (char *)NULL);
        execlp("firefox", "firefox", url, (char *)NULL);
        _exit(1);
    }
}

static void send_http_response(int fd, int status_code, const char *status_text,
                               const char *content_type, const char *body)
{
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             status_code, status_text, content_type, strlen(body));
    ssize_t w = write(fd, header, strlen(header));
    (void)w;
    w = write(fd, body, strlen(body));
    (void)w;
}

static void send_login_page(int client_fd, const char *suggested_user, const char *error_msg)
{
    char html[8192];
    char error_block[512] = "";
    if (error_msg && strlen(error_msg) > 0) {
        snprintf(error_block, sizeof(error_block),
                 "<div class=\"err-alert\"><strong>&#10008; Access Denied:</strong> %s</div>", error_msg);
    }

    const char *default_emp_id = "";
    if (suggested_user) {
        if (strcasecmp(suggested_user, "Alice") == 0) default_emp_id = "EMP001";
        else if (strcasecmp(suggested_user, "Bob") == 0) default_emp_id = "EMP002";
        else if (strcasecmp(suggested_user, "Charlie") == 0) default_emp_id = "EMP003";
        else if (strcasecmp(suggested_user, "Rey") == 0) default_emp_id = "EMP101";
    }

    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<title>Employee Authentication &mdash; Chat Messenger</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}"
        "body{background:linear-gradient(135deg,#090d16 0%%,#111827 50%%,#090d16 100%%);color:#f3f4f6;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
        ".card{background:rgba(31,41,55,0.85);border:1px solid rgba(255,255,255,0.1);border-radius:16px;padding:36px;width:100%%;max-width:480px;box-shadow:0 25px 50px rgba(0,0,0,0.6);backdrop-filter:blur(12px);}"
        ".header{text-align:center;margin-bottom:24px;}"
        ".icon{width:56px;height:56px;margin:0 auto 12px;background:rgba(59,130,246,0.15);border:2px solid #3b82f6;border-radius:50%%;display:flex;align-items:center;justify-content:center;font-size:26px;color:#60a5fa;}"
        "h1{font-size:22px;font-weight:700;color:#fff;margin-bottom:6px;}"
        ".sub{font-size:12px;color:#9ca3af;text-transform:uppercase;letter-spacing:1px;}"
        ".err-alert{padding:12px 14px;border-radius:8px;font-size:13px;margin-bottom:18px;background:rgba(239,68,68,0.15);border:1px solid #ef4444;color:#fca5a5;}"
        ".fg{margin-bottom:18px;}"
        "label{display:block;font-size:12px;font-weight:600;color:#d1d5db;margin-bottom:6px;text-transform:uppercase;letter-spacing:0.5px;}"
        "input{width:100%%;padding:12px 14px;background:#111827;border:1px solid #374151;border-radius:8px;color:#fff;font-size:15px;outline:none;transition:border-color 0.2s;}"
        "input:focus{border-color:#3b82f6;}"
        ".btn{width:100%%;padding:12px;background:linear-gradient(90deg,#2563eb,#1d4ed8);border:none;border-radius:8px;color:#fff;font-size:15px;font-weight:600;cursor:pointer;}"
        ".btn:hover{background:linear-gradient(90deg,#1d4ed8,#1e40af);}"
        ".chips-box{margin-top:24px;padding-top:18px;border-top:1px solid rgba(255,255,255,0.08);}"
        ".chips-title{font-size:11px;font-weight:600;color:#9ca3af;text-transform:uppercase;margin-bottom:10px;text-align:center;}"
        ".chips{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;}"
        ".chip{background:#374151;color:#e5e7eb;border:1px solid #4b5563;border-radius:20px;padding:4px 10px;font-size:11px;cursor:pointer;}"
        ".chip:hover{background:#4b5563;color:#fff;}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<div class=\"header\"><div class=\"icon\">&#128274;</div><h1>Employee Access Portal</h1><div class=\"sub\">Operating Systems Secure Chat</div></div>"
        "%s"
        "<form action=\"/login\" method=\"POST\">"
        "<div class=\"fg\"><label>Employee ID</label><input type=\"text\" id=\"eid\" name=\"employee_id\" value=\"%s\" placeholder=\"e.g. EMP001\" required autofocus></div>"
        "<div class=\"fg\"><label>Corporate Password</label><input type=\"password\" id=\"pw\" name=\"password\" placeholder=\"&bull;&bull;&bull;&bull;&bull;&bull;\" required></div>"
        "<button type=\"submit\" class=\"btn\">Confirm Identity &amp; Launch Chat</button>"
        "</form>"
        "<div class=\"chips-box\"><div class=\"chips-title\">Test Employee Accounts</div><div class=\"chips\">"
        "<span class=\"chip\" onclick=\"f('EMP001','alice123')\">Alice (EMP001)</span>"
        "<span class=\"chip\" onclick=\"f('EMP002','bob123')\">Bob (EMP002)</span>"
        "<span class=\"chip\" onclick=\"f('EMP003','charlie123')\">Charlie (EMP003)</span>"
        "<span class=\"chip\" onclick=\"f('EMP101','osproject2026')\">Rey (EMP101)</span>"
        "</div></div></div>"
        "<script>function f(i,p){document.getElementById('eid').value=i;document.getElementById('pw').value=p;}</script>"
        "</body></html>",
        error_block, default_emp_id);

    send_http_response(client_fd, 200, "OK", "text/html; charset=UTF-8", html);
}

static void send_success_page(int client_fd, const auth_employee_t *emp)
{
    char html[4096];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Authentication Verified</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}"
        "body{background:linear-gradient(135deg,#064e3b 0%%,#0f172a 100%%);color:#f3f4f6;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
        ".card{background:rgba(31,41,55,0.9);border:1px solid rgba(16,185,129,0.3);border-radius:16px;padding:36px;width:100%%;max-width:460px;text-align:center;box-shadow:0 25px 50px rgba(0,0,0,0.6);}"
        ".icon{width:60px;height:60px;margin:0 auto 14px;background:rgba(16,185,129,0.2);border:2px solid #10b981;border-radius:50%%;display:flex;align-items:center;justify-content:center;font-size:28px;color:#34d399;}"
        "h1{font-size:22px;font-weight:700;color:#fff;margin-bottom:8px;}"
        ".badge{display:inline-block;background:#065f46;color:#a7f3d0;border-radius:6px;padding:6px 12px;font-size:13px;font-weight:600;margin:10px 0 18px;}"
        ".info{background:rgba(17,24,39,0.6);border-radius:8px;padding:12px;text-align:left;font-size:13px;margin-bottom:20px;}"
        ".row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid rgba(255,255,255,0.05);}"
        ".row:last-child{border-bottom:none;}"
        ".lbl{color:#9ca3af;font-weight:600;}.val{color:#fff;}"
        ".desc{font-size:13px;color:#d1d5db;line-height:1.5;margin-bottom:14px;}"
        ".note{font-size:11px;color:#9ca3af;}"
        "</style></head><body><div class=\"card\">"
        "<div class=\"icon\">&#10004;</div><h1>Authentication Verified</h1>"
        "<div class=\"badge\">%s &bull; %s</div>"
        "<div class=\"info\">"
        "<div class=\"row\"><span class=\"lbl\">Employee Name</span><span class=\"val\">%s</span></div>"
        "<div class=\"row\"><span class=\"lbl\">Employee ID</span><span class=\"val\">%s</span></div>"
        "<div class=\"row\"><span class=\"lbl\">Department</span><span class=\"val\">%s</span></div>"
        "<div class=\"row\"><span class=\"lbl\">Status</span><span class=\"val\" style=\"color:#34d399;\">Authorized</span></div>"
        "</div>"
        "<p class=\"desc\">Your employee session is verified. You may close this browser tab and return to your terminal to begin chatting.</p>"
        "<div class=\"note\">Window will close automatically.</div></div>"
        "<script>setTimeout(function(){window.close();},3500);</script></body></html>",
        emp->name, emp->employee_id,
        emp->name, emp->employee_id, emp->department);

    send_http_response(client_fd, 200, "OK", "text/html; charset=UTF-8", html);
}

static void extract_form_field(const char *data, const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    if (!data || !key) return;

    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(data, search);
    if (!p) return;

    p += strlen(search);
    char raw[256] = {0};
    size_t i = 0;
    while (*p && *p != '&' && *p != ' ' && *p != '\r' && *p != '\n' && i + 1 < sizeof(raw)) {
        raw[i++] = *p++;
    }
    raw[i] = '\0';
    url_decode(out, raw, out_size);
    trim_whitespace(out);
}

int auth_browser_login(const char *suggested_user, auth_session_t *out_session)
{
    if (!out_session) return -1;
    memset(out_session, 0, sizeof(*out_session));

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("Failed to create authentication loopback socket");
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 */

    int bound_port = 0;
    int try_ports[] = {AUTH_DEFAULT_PORT, 8586, 8587, 8588, 0};
    for (size_t i = 0; i < sizeof(try_ports)/sizeof(try_ports[0]); i++) {
        serv_addr.sin_port = htons(try_ports[i]);
        if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            socklen_t addr_len = sizeof(serv_addr);
            if (getsockname(listen_fd, (struct sockaddr *)&serv_addr, &addr_len) == 0) {
                bound_port = ntohs(serv_addr.sin_port);
                break;
            }
        }
    }

    if (bound_port == 0 || listen(listen_fd, 5) < 0) {
        LOG_ERROR("Failed to bind and listen on authentication loopback port");
        close(listen_fd);
        return -1;
    }

    char login_url[128];
    snprintf(login_url, sizeof(login_url), "http://127.0.0.1:%d/login", bound_port);

    printf("\n" COLOR_BOLD "========================================================================" COLOR_RESET "\n");
    printf(COLOR_CYAN COLOR_BOLD "  SECURE EMPLOYEE BROWSER AUTHENTICATION PORTAL" COLOR_RESET "\n");
    printf(COLOR_BOLD "========================================================================" COLOR_RESET "\n");
    printf("  To maintain communications security, corporate employee verification\n");
    printf("  is required before accessing the chat.\n\n");
    printf("  Redirecting to login portal in your default browser:\n");
    printf("  " COLOR_GREEN COLOR_BOLD "%s" COLOR_RESET "\n\n", login_url);
    printf("  " COLOR_YELLOW "Waiting for authentication in browser... (or press ENTER for terminal login)" COLOR_RESET "\n");
    printf(COLOR_BOLD "========================================================================" COLOR_RESET "\n\n");
    fflush(stdout);

    /* Open browser via xdg-open child process */
    open_browser_process(login_url);

    time_t start_time = time(NULL);
    bool auth_success = false;

    while (!auth_success) {
        time_t now = time(NULL);
        if (now - start_time > AUTH_TIMEOUT_SEC) {
            printf("\n" COLOR_RED "[AUTH ERROR] Authentication session timed out (%d seconds)." COLOR_RESET "\n", AUTH_TIMEOUT_SEC);
            break;
        }

        struct pollfd pfd[2];
        pfd[0].fd = listen_fd;
        pfd[0].events = POLLIN;
        pfd[1].fd = STDIN_FILENO;
        pfd[1].events = POLLIN;

        int ret = poll(pfd, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Check for terminal fallback if user presses Enter */
        if (pfd[1].revents & POLLIN) {
            char term_buf[128] = {0};
            if (fgets(term_buf, sizeof(term_buf), stdin)) {
                proto_strip_newline(term_buf);
                trim_whitespace(term_buf);

                char emp_id[MAX_EMP_ID_LEN] = {0};
                char pass[MAX_EMP_PASS_LEN] = {0};

                if (strlen(term_buf) > 0) {
                    snprintf(emp_id, sizeof(emp_id), "%s", term_buf);
                } else {
                    printf("\n" COLOR_BOLD "--- Terminal Authentication Fallback ---" COLOR_RESET "\n");
                    printf("Enter Employee ID (e.g. EMP001, EMP002, EMP101): ");
                    if (fgets(emp_id, sizeof(emp_id), stdin)) proto_strip_newline(emp_id);
                }

                printf("Enter Corporate Password: ");
                if (fgets(pass, sizeof(pass), stdin)) proto_strip_newline(pass);
                trim_whitespace(emp_id);
                trim_whitespace(pass);

                auth_employee_t emp;
                if (auth_verify_credentials(emp_id, pass, &emp) == 0) {
                    snprintf(out_session->employee_id, sizeof(out_session->employee_id), "%s", emp.employee_id);
                    snprintf(out_session->employee_name, sizeof(out_session->employee_name), "%s", emp.name);
                    snprintf(out_session->department, sizeof(out_session->department), "%s", emp.department);
                    snprintf(out_session->token, sizeof(out_session->token), "AUTH-%08lx%04x", (unsigned long)time(NULL), rand() & 0xFFFF);
                    out_session->authenticated = true;
                    out_session->login_time = time(NULL);
                    auth_success = true;
                    printf(COLOR_GREEN "[AUTH SUCCESS] Verified Employee: %s (%s) - %s" COLOR_RESET "\n",
                           emp.name, emp.employee_id, emp.department);
                    break;
                } else {
                    printf(COLOR_RED "[AUTH ERROR] Invalid Employee ID or Password. Try again or complete in browser." COLOR_RESET "\n\n");
                }
            }
        }

        if (!(pfd[0].revents & POLLIN)) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char req[4096] = {0};
        ssize_t bytes_in = read(client_fd, req, sizeof(req) - 1);
        if (bytes_in <= 0) {
            close(client_fd);
            continue;
        }

        if (strstr(req, "GET /favicon.ico")) {
            send_http_response(client_fd, 404, "Not Found", "text/plain", "Not Found");
            close(client_fd);
            continue;
        }

        char emp_id[MAX_EMP_ID_LEN] = {0};
        char password[MAX_EMP_PASS_LEN] = {0};

        /* Check for GET query parameters: GET /login?employee_id=...&password=... */
        char *q = strstr(req, "GET /login?");
        if (q) {
            q += strlen("GET /login?");
            extract_form_field(q, "employee_id", emp_id, sizeof(emp_id));
            extract_form_field(q, "password", password, sizeof(password));
        }

        /* Check for POST body */
        if (strncmp(req, "POST", 4) == 0) {
            char *body = strstr(req, "\r\n\r\n");
            if (body) {
                body += 4;
                extract_form_field(body, "employee_id", emp_id, sizeof(emp_id));
                extract_form_field(body, "password", password, sizeof(password));
            }
        }

        if (strlen(emp_id) > 0 && strlen(password) > 0) {
            auth_employee_t emp;
            if (auth_verify_credentials(emp_id, password, &emp) == 0) {
                send_success_page(client_fd, &emp);
                close(client_fd);

                snprintf(out_session->employee_id, sizeof(out_session->employee_id), "%s", emp.employee_id);
                snprintf(out_session->employee_name, sizeof(out_session->employee_name), "%s", emp.name);
                snprintf(out_session->department, sizeof(out_session->department), "%s", emp.department);
                snprintf(out_session->token, sizeof(out_session->token), "AUTH-%08lx%04x", (unsigned long)time(NULL), rand() & 0xFFFF);
                out_session->authenticated = true;
                out_session->login_time = time(NULL);

                printf(COLOR_GREEN "[AUTH SUCCESS] Browser Verified Employee: %s (%s) &mdash; Department: %s" COLOR_RESET "\n",
                       emp.name, emp.employee_id, emp.department);
                printf(COLOR_CYAN "[SECURITY] Access granted. Proceeding to chat session..." COLOR_RESET "\n\n");
                auth_success = true;
                break;
            } else {
                printf(COLOR_YELLOW "[AUTH WARN] Browser login attempt failed for Employee ID: '%s'" COLOR_RESET "\n", emp_id);
                send_login_page(client_fd, suggested_user, "Invalid Employee ID or Password. Non-employees cannot access the chat.");
                close(client_fd);
            }
        } else {
            /* Standard GET request: serve login portal */
            send_login_page(client_fd, suggested_user, NULL);
            close(client_fd);
        }
    }

    close(listen_fd);
    return auth_success ? 0 : -1;
}

int auth_save_session(const char *filepath, const auth_session_t *session)
{
    if (!filepath || !session) return -1;
    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;

    fprintf(fp, "employee_id=%s\n", session->employee_id);
    fprintf(fp, "employee_name=%s\n", session->employee_name);
    fprintf(fp, "department=%s\n", session->department);
    fprintf(fp, "token=%s\n", session->token);
    fprintf(fp, "login_time=%ld\n", (long)session->login_time);
    fclose(fp);
    return 0;
}

int auth_load_session(const char *filepath, auth_session_t *session)
{
    if (!filepath || !session) return -1;
    FILE *fp = fopen(filepath, "r");
    if (!fp) return -1;

    memset(session, 0, sizeof(*session));
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        proto_strip_newline(line);
        char key[64] = {0};
        char val[192] = {0};
        if (sscanf(line, "%63[^=]=%191[^\n]", key, val) == 2) {
            if (strcmp(key, "employee_id") == 0) snprintf(session->employee_id, sizeof(session->employee_id), "%s", val);
            else if (strcmp(key, "employee_name") == 0) snprintf(session->employee_name, sizeof(session->employee_name), "%s", val);
            else if (strcmp(key, "department") == 0) snprintf(session->department, sizeof(session->department), "%s", val);
            else if (strcmp(key, "token") == 0) snprintf(session->token, sizeof(session->token), "%s", val);
            else if (strcmp(key, "login_time") == 0) session->login_time = atol(val);
        }
    }
    fclose(fp);

    /* Session valid for 24 hours */
    if (strlen(session->employee_id) > 0 && time(NULL) - session->login_time < 86400) {
        session->authenticated = true;
        return 0;
    }
    return -1;
}
