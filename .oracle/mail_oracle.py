import json, mailbox, email.header, email.utils, sys

path = sys.argv[1]
out = []
box = mailbox.mbox(path, create=False)
for key in box.iterkeys():
    msg = box.get(key)
    raw_subject = msg.get('Subject', '')
    try:
        subject = str(email.header.make_header(email.header.decode_header(raw_subject)))
    except Exception:
        subject = raw_subject
    date_raw = msg.get('Date', '')
    date_utc = ''
    try:
        parsed = email.utils.parsedate_to_datetime(date_raw)
        date_utc = parsed.astimezone(email.utils.datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    except Exception:
        date_utc = ''
    body = ''
    html_only = False
    if msg.is_multipart():
        for part in msg.walk():
            ctype = part.get_content_type()
            if ctype == 'text/plain' and not body:
                payload = part.get_payload(decode=True)
                if payload is not None:
                    body = payload.decode('utf-8', 'replace')
            elif ctype == 'text/html' and not body:
                html_only = True
    else:
        if msg.get_content_type() == 'text/html':
            html_only = True
        payload = msg.get_payload(decode=True)
        if payload is not None:
            body = payload.decode('utf-8', 'replace')
    out.append({
        'date_utc': date_utc,
        'subject': subject,
        'body': body.replace('\r\n', '\n').strip(),
        'html_only': html_only,
    })
json.dump(out, open(sys.argv[2], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('oracle_messages', len(out))
