// Etc::LogParser: thirteen regular expressions, each counted over a
// generated web server log with the `regex` module, as the Rust version
// counts them with the regex crate's find_iter().count().

namespace log_parser;

import std;
import dictionary;
import bench;
import regex;

let NAMES = [
    "errors", "bots", "suspicious", "ips", "api_calls", "post_requests", "auth_attempts",
    "methods", "emails", "passwords", "tokens", "sessions", "peak_hours",
];
let PATTERNS = [
    """ [5][0-9]{2} | [4][0-9]{2} """,
    """(?i)bot|crawler|scanner|spider|indexing|crawl|robot|spider""",
    """(?i)etc/passwd|wp-admin|\.\./""",
    """\d+\.\d+\.\d+\.35""",
    """/api/[^ "]+""",
    """POST [^ ]* HTTP""",
    """(?i)/login|/signin""",
    """(?i)get|post|put""",
    """[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}""",
    """password=[^&\s"]+""",
    """token=[^&\s"]+|api[_-]?key=[^&\s"]+""",
    """session[_-]?id=[^&\s"]+""",
    """\[\d+/\w+/\d+:1[3-7]:\d+:\d+ [+\-]\d+\]""",
];

let METHODS = ["GET", "POST", "PUT", "DELETE"];
let PATHS = ["/index.html", "/api/users", "/admin", "/images/logo.png", "/etc/passwd", "/wp-admin/setup.php"];
let STATUSES = [200, 201, 301, 302, 400, 401, 403, 404, 500, 502, 503];
let AGENTS = ["Mozilla/5.0", "Googlebot/2.1", "curl/7.68.0", "scanner/2.0"];
let USERS = ["john", "jane", "alex", "sarah", "mike", "anna", "david", "elena"];
let DOMAINS = ["example.com", "gmail.com", "yahoo.com", "hotmail.com", "company.org", "mail.ru"];

fn generate_line(log: u8[>..]&, i: i64) {
    format(log, "192.168.1.", i % 255 + 1, " - - [", i % 31, "/Oct/2023:", i % 60, ":55:36 +0000] \"",
           METHODS[i % METHODS.len], " ");
    if i % 3 == 0 {
        format(log, "/login?email=", USERS[i % USERS.len], i % 100, "@", DOMAINS[i % DOMAINS.len],
               "&password=secret", i % 10000);
    } else if i % 5 == 0 {
        log.append("/api/data?token=");
        for k in i % 3 + 1 { log.append("abcdef123456"); }
    } else if i % 7 == 0 {
        log.append("/user/profile?session_id=sess_");
        format_uint(log, (i * 12345) as u64, 16, 0, ' ');
    } else {
        log.append(PATHS[i % PATHS.len]);
    }
    format(log, " HTTP/1.1\" ", STATUSES[i % STATUSES.len], " 2326 \"http://", DOMAINS[i % DOMAINS.len],
           "\" \"", AGENTS[i % AGENTS.len], "\"\n");
}

fn run(b: Bench&) -> u32 {
    let lines = config_i64(b.name, "lines_count");
    var log: u8[>..] = [];
    for i in lines { generate_line(log, i); }
    var patterns: regex::Regex[>..] = [];
    for p in PATTERNS { patterns.push(regex::compile(p)); }
    var checksum: u32 = 0;
    measure(b) {
        var matches = dictionary<const u8[:], i64> {};
        for re, k in patterns { matches.insert(NAMES[k], regex::count(re, log)); }
        var total = 0;
        matches.each() { name, n => total += n; };
        checksum += total as! u32;
    };
    return checksum;
}
