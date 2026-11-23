#!/usr/bin/env python3
"""Fake GitHub API for the "APK on GitHub" test: repositories, git objects, a workflow run that ends at
once and a release with the APK (download through a redirect, like GitHub's file storage).
usage: fake_github.py <port> <log> [falhar]"""
import sys, json, hashlib, base64, http.server, re
port, logf = int(sys.argv[1]), sys.argv[2]
fail = len(sys.argv) > 3
TOKEN = "chave-de-teste"
repos, blobs, trees, commits, refs = {}, {}, {}, {}, {}
def log(*a):
    with open(logf, "a") as f: f.write(" ".join(str(x) for x in a) + "\n")
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send(self, code, obj=None, raw=None, headers=None):
        b = raw if raw is not None else (json.dumps(obj).encode() if obj is not None else b"")
        self.send_response(code)
        for k, v in (headers or {}).items(): self.send_header(k, v)
        self.send_header("Content-Length", str(len(b))); self.send_header("Content-Type", "application/json"); self.end_headers()
        self.wfile.write(b)
    def body(self):
        n = int(self.headers.get("Content-Length", 0)); return json.loads(self.rfile.read(n) or b"{}")
    def auth(self):
        if self.path.startswith("/storage/"): return True
        if self.headers.get("Authorization") != "Bearer " + TOKEN: self.send(401, {"message": "Bad credentials"}); return False
        return True
    def do_GET(self):
        if not self.auth(): return
        p = self.path
        log("GET", p)
        if p == "/user": return self.send(200, {"login": "adryan"})
        m = re.match(r"/repos/adryan/([^/?]+)(.*)", p)
        if p.startswith("/storage/"): return self.send(200, raw=b"PK\x03\x04" + b"APK-DE-TESTE" * 200)
        if not m: return self.send(404, {"message": "Not Found"})
        r, rest = m.group(1), m.group(2)
        if r not in repos: return self.send(404, {"message": "Not Found"})
        if rest == "": return self.send(200, {"name": r, "private": True})
        if rest == "/git/ref/heads/main":
            if r not in refs: return self.send(409, {"message": "Git Repository is empty."})
            return self.send(200, {"ref": "refs/heads/main", "object": {"sha": refs[r], "type": "commit"}})
        m2 = re.match(r"/git/trees/([0-9a-f]+)", rest)
        if m2:
            c = commits.get(m2.group(1)); t = trees.get(c["tree"]) if c else {"tree": []}
            return self.send(200, {"sha": c["tree"] if c else "", "tree": [{"path": e["path"], "mode": "100644", "type": "blob", "sha": e["sha"], "size": 1} for e in t["tree"]]})
        if rest.startswith("/actions/runs"):
            sha = re.search(r"head_sha=([0-9a-f]+)", rest).group(1)
            return self.send(200, {"total_count": 1, "workflow_runs": [{"id": 777, "name": "apk", "head_sha": sha, "status": "completed",
                                    "conclusion": "failure" if fail else "success", "html_url": "https://github.com/adryan/%s/actions/runs/777" % r}]})
        m3 = re.match(r"/releases/tags/apk-([0-9a-f]{7})", rest)
        if m3:
            return self.send(200, {"tag_name": "apk-" + m3.group(1), "assets": [{"url": "http://127.0.0.1:%d/repos/adryan/%s/releases/assets/9" % (port, r), "id": 9, "name": "Jogo.apk"}]})
        if rest == "/releases/assets/9":
            if self.headers.get("Accept") != "application/octet-stream": return self.send(415, {"message": "accept"})
            return self.send(302, raw=b"", headers={"Location": "http://127.0.0.1:%d/storage/jogo.apk" % port})
        return self.send(404, {"message": "Not Found"})
    def do_POST(self):
        if not self.auth(): return
        b = self.body(); p = self.path
        log("POST", p)
        if p == "/user/repos":
            repos[b["name"]] = b; log("repo_criado", b["name"], "privado=%s" % b.get("private")); return self.send(201, {"name": b["name"]})
        m = re.match(r"/repos/adryan/([^/]+)/git/(blobs|trees|commits)", p)
        if not m: return self.send(404, {"message": "Not Found"})
        r, kind = m.group(1), m.group(2)
        if kind == "blobs":
            data = base64.b64decode(b["content"]); sha = hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest(); blobs[sha] = data
            return self.send(201, {"sha": sha})
        if kind == "trees":
            for e in b["tree"]:
                if e["sha"] not in blobs: log("ERRO_blob_desconhecido", e["path"]); return self.send(422, {"message": "unknown blob"})
            sha = hashlib.sha1(json.dumps(b, sort_keys=True).encode()).hexdigest(); trees[sha] = b
            log("arvore", len(b["tree"]), "arquivos", "workflow" if any(e["path"] == ".github/workflows/apk.yml" for e in b["tree"]) else "SEM_WORKFLOW")
            return self.send(201, {"sha": sha})
        sha = hashlib.sha1(json.dumps(b, sort_keys=True).encode()).hexdigest(); commits[sha] = b
        return self.send(201, {"sha": sha})
    def do_PUT(self):
        if not self.auth(): return
        b = self.body(); m = re.match(r"/repos/adryan/([^/]+)/contents/(.+)", self.path)
        log("PUT", self.path)
        r = m.group(1); data = base64.b64decode(b["content"]); bs = hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest(); blobs[bs] = data
        t = {"tree": [{"path": m.group(2), "sha": bs}]}; ts = hashlib.sha1(json.dumps(t).encode()).hexdigest(); trees[ts] = t
        commits["c0" * 20] = {"tree": ts}; refs[r] = "c0" * 20
        return self.send(201, {"content": {"sha": bs}})
    def do_PATCH(self):
        if not self.auth(): return
        b = self.body(); m = re.match(r"/repos/adryan/([^/]+)/git/refs/heads/main", self.path)
        log("PATCH", self.path); refs[m.group(1)] = b["sha"]
        return self.send(200, {"object": {"sha": b["sha"]}})
http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
