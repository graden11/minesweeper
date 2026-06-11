import urllib.request, json, time, http.cookiejar

cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

# Login
r = json.loads(op.open(urllib.request.Request('http://localhost:80/login',
    data=json.dumps({"username":"user1","password":"123456"}).encode(),
    headers={'Content-Type':'application/json'})).read().decode())
print("Login:", r['status'])

# Get current
d = json.loads(op.open(urllib.request.Request('http://localhost:80/system/hardware')).read().decode())
c = d['current']
print("Before: threads=%d batch=%d delay=%dms rate=%d" % (c['server_threads'], c['max_batch_size'], c['max_delay_ms'], c['rate_limit_req_per_sec']))

# Apply aggressive
target_key = 'aggressive'
target = d['profiles'][target_key]['params']
print("Applying %s (threads=%d batch=%d delay=%dms rate=%d)..." % (target_key, target['server_threads'], target['max_batch_size'], target['max_delay_ms'], target['rate_limit_req_per_sec']))
r = json.loads(op.open(urllib.request.Request('http://localhost:80/system/config/apply',
    data=json.dumps({"profile":target_key}).encode(),
    headers={'Content-Type':'application/json'})).read().decode())
print("Apply:", r['status'])

# Restart
print("Restarting...")
t0 = time.time()
try:
    op.open(urllib.request.Request('http://localhost:80/system/restart', data=b'', headers={'Content-Type':'application/json'}), timeout=2)
except:
    pass

# Wait for health
for i in range(60):
    try:
        if json.loads(op.open(urllib.request.Request('http://localhost:80/health'), timeout=2).read().decode()).get('status') == 'ok':
            elapsed = time.time() - t0
            d2 = json.loads(op.open(urllib.request.Request('http://localhost:80/system/hardware')).read().decode())
            c2 = d2['current']
            match = all(c2[k] == target[k] for k in ['server_threads','max_batch_size','max_delay_ms','rate_limit_req_per_sec'])
            print("Restart: %.2fs | threads=%d batch=%d delay=%dms rate=%d | match: %s" % (elapsed, c2['server_threads'], c2['max_batch_size'], c2['max_delay_ms'], c2['rate_limit_req_per_sec'], match))
            break
    except:
        pass
    time.sleep(1)
else:
    print("TIMEOUT")
