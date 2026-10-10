// Etc::LogParser: thirteen regular expressions, each counted over a
// generated web server log with the `regex` module, as the Rust version
// counts them with the regex crate's find_iter().count().

namespace log_parser;

import std;
import dictionary;
import bench;
import regex;

// A name and its regular expression; the reference's (&str, Regex) tuple.
struct Pattern { name: const u8[:], regex: regex::Regex }

let PATTERNS = [
    Pattern { "errors", regex::compile(""" [5][0-9]{2} | [4][0-9]{2} """) },
    Pattern { "bots", regex::compile("""(?i)bot|crawler|scanner|spider|indexing|crawl|robot|spider""") },
    Pattern { "suspicious", regex::compile("""(?i)etc/passwd|wp-admin|\.\./""") },
    Pattern { "ips", regex::compile("""\d+\.\d+\.\d+\.35""") },
    Pattern { "api_calls", regex::compile("""/api/[^ "]+""") },
    Pattern { "post_requests", regex::compile("""POST [^ ]* HTTP""") },
    Pattern { "auth_attempts", regex::compile("""(?i)/login|/signin""") },
    Pattern { "methods", regex::compile("""(?i)get|post|put""") },
    Pattern { "emails", regex::compile("""[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}""") },
    Pattern { "passwords", regex::compile("""password=[^&\s"]+""") },
    Pattern { "tokens", regex::compile("""token=[^&\s"]+|api[_-]?key=[^&\s"]+""") },
    Pattern { "sessions", regex::compile("""session[_-]?id=[^&\s"]+""") },
    Pattern { "peak_hours", regex::compile("""\[\d+/\w+/\d+:1[3-7]:\d+:\d+ [+\-]\d+\]""") },
];

// "192.168.1.1" to "192.168.1.255", each in a limited array, as the elements
// of an indexable array are fixed-size; an IPv4 address takes at most 15
// characters.
let IPS = generate_ips();

fn generate_ips() -> u8[..15][>..] {
    var ips: u8[..15][>..] = [];
    for i in 1..256 { ips.push(str("192.168.1.", i)); }
    return ips;
}

let METHODS = ["GET", "POST", "PUT", "DELETE"];
let PATHS = ["/index.html", "/api/users", "/admin", "/images/logo.png", "/etc/passwd", "/wp-admin/setup.php"];
let STATUSES = [200, 201, 301, 302, 400, 401, 403, 404, 500, 502, 503];
let AGENTS = ["Mozilla/5.0", "Googlebot/2.1", "curl/7.68.0", "scanner/2.0"];
let USERS = ["john", "jane", "alex", "sarah", "mike", "anna", "david", "elena"];
let DOMAINS = ["example.com", "gmail.com", "yahoo.com", "hotmail.com", "company.org", "mail.ru"];

fn generate_log_line(log: u8[>..]&, i: i64) {
    format(log, IPS[i % IPS.len], " - - [", i % 31, "/Oct/2023:", i % 60, ":55:36 +0000] \"",
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
    let lines_count = config_i64(b.name, "lines_count");
    var log: u8[>..] = [];
    for i in lines_count { generate_log_line(log, i); }
    var checksum: u32 = 0;
    measure(b) {
        var matches = dictionary<const u8[:], i64> {};
        for p in PATTERNS { matches.insert(p.name, regex::count(p.regex, log)); }
        var total = 0;
        matches.each() { name, count => total += count; };
        checksum += total as! u32;
    };
    return checksum;
}
