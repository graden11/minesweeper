import urllib.request, json, time, http.cookiejar, os

# Use cookie jar for session
cj = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

# Login
req = urllib.request.Request('http://localhost:80/login',
    data=json.dumps({"username":"user1","password":"123456"}).encode(),
    headers={'Content-Type':'application/json'})
resp = json.loads(opener.open(req).read().decode())
print("Login:", resp['status'])

# Get current config
req = urllib.request.Request('http://localhost:80/system/hardware')
hw = json.loads(opener.open(req).read().decode())
cur = hw['current']
stable = hw['profiles']['stable']['params']
print("Before: threads=%d batch=%d" % (cur['server_threads'], cur['max_batch_size']))
print("Applying stable profile...")

# Apply
req = urllib.request.Request('http://localhost:80/system/config/apply',
    data=json.dumps({"profile":"stable"}).encode(),
    headers={'Content-Type':'application/json'})
resp = json.loads(opener.open(req).read().decode())
print("Apply:", resp['status'])

# Restart
print("Triggering restart...")
t0 = time.time()
try:
    urllib.request.urlopen(urllib.request.Request('http://localhost:80/system/restart',
        data=b'', headers={'Content-Type':'application/json'}), timeout=2)
except:
    pass  # expected to fail during shutdown

# Wait for health
for i in range(60):
    try:
        req = urllib.request.Request('http://localhost:80/health')
        d = json.loads(opener.open(req, timeout=2).read().decode())
        if d.get('status') == 'ok':
            elapsed = time.time() - t0
            print("Service back in %.2f seconds" % elapsed)

            # Verify config
            req = urllib.request.Request('http://localhost:80/system/hardware')
            hw2 = json.loads(opener.open(req).read().decode())
            cur2 = hw2['current']
            match = all(cur2[k] == stable[k] for k in ['server_threads','max_batch_size','max_delay_ms','rate_limit_req_per_sec'])
            print("After:  threads=%d batch=%d delay=%dms rate=%d -> match stable: %s" % (
                cur2['server_threads'], cur2['max_batch_size'], cur2['max_delay_ms'], cur2['rate_limit_req_per_sec'], match))
            break
    except:
        pass
    time.sleep(1)
else:
    print("TIMEOUT: service did not come back in 60s")
