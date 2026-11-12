import sqlite3, sys
path = sys.argv[1]
c = sqlite3.connect(path)
tables = [r[0] for r in c.execute("select name from sqlite_master where type='table'")]
print('tables:', tables)
for t in tables:
    cols = [r[1] for r in c.execute('pragma table_info("%s")' % t)]
    n = c.execute('select count(*) from "%s"' % t).fetchone()[0]
    print(' ', t, n, cols)
    if 'media_key' in cols and 'view_once' in cols:
        print('    view_once rows:', c.execute('select count(*) from "%s" where view_once is not null and view_once != 0' % t).fetchone()[0])