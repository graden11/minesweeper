import urllib.request, json, time, http.cookiejar

cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

def run():
    # Login
    r = json.loads(op.open(urllib.request.Request('http://localhost:80/login',
        data=json.dumps({"username":"user1","password":"123456"}).encode(),
        headers={'Content-Type':'application/json'})).read().decode())
    assert r['status'] == 'ok', f"Login failed: {r}"
    print("Login: OK")

    # Get current
    d = json.loads(op.open(urllib.request.Request('http://localhost:80/system/hardware')).read().decode())
    c = d['current']
    print("Before: threads=%d batch=%d delay=%dms rate=%d" % (
        c['server_threads'], c['max_batch_size'], c['max_delay_ms'], c['rate_limit_req_per_sec']))

    # Apply aggressive
    target = d['profiles']['aggressive']['params']
    r = json.loads(op.open(urllib.request.Request('http://localhost:80/system/config/apply',
        data=json.dumps({"profile":"aggressive"}).encode(),
        headers={'Content-Type':'application/json'})).read().decode())
    print("Apply aggressive: %s" % r['status'])

    # Restart
    print("Restarting...")
    t0 = time.time()
    try:
        op.open(urllib.request.Request('http://localhost:80/system/restart', data=b'',
            headers={'Content-Type':'application/json'}), timeout=2)
    except:
        pass

    # Wait
    for i in range(60):
        try:
            if json.loads(op.open(urllib.request.Request('http://localhost:80/health'), timeout=2).read().decode()).get('status') == 'ok':
                elapsed = time.time() - t0
                d2 = json.loads(op.open(urllib.request.Request('http://localhost:80/system/hardware')).read().decode())
                c2 = d2['current']
                match = all(c2[k] == target[k] for k in ['server_threads','max_batch_size','max_delay_ms','rate_limit_req_per_sec'])
                print("Restart: %.1fs | threads=%d batch=%d delay=%dms rate=%d | match: %s" % (
                    elapsed, c2['server_threads'], c2['max_batch_size'], c2['max_delay_ms'], c2['rate_limit_req_per_sec'], match))
                return match
        except:
            pass
        time.sleep(1)
    print("TIMEOUT")
    return False

if __name__ == '__main__':
    ok = run()
    exit(0 if ok else 1)
