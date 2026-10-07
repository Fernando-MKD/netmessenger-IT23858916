Design Diary - NetMessenger (IE3010, IT23858916)
Start and timeline
I started this assignment late (7 October 2026, the deadline day), so my commits are all from that day and are not spread over the assignment period.
Order of work: GitHub setup -> personalised values -> Makefile -> server -> client -> manual testing with screenshots -> report -> documents -> submission.

Key decisions
Personalised values. Port 6000 + 8916 = 14916; NID = digits 3-6 of 23858916 = 8589; file names use 8916; log file netmsg_IT23858916.log; storage ./storage/IT23858916/<sender>/<filename>.
Concurrency model: one thread per client (pthreads) + one mutex. I first looked at poll() (single process, no locks) but chose threads because blocking recv() per client is simpler for me to write and explain. It is easier for me to understand
Framing. read_line() reads one byte at a time until '\n', so it never reads into file bytes that follow a SENDFILE line. recv_bytes() loops until exactly <filesize> bytes are received.
Errors during SENDFILE. The client sends the file bytes anyway, so on any error the server still reads and discards them. Otherwise they would be parsed as commands.
NID tag. Stored as one string constant glued onto every OK/ERR reply, so it cannot be forgotten.
File safety. Usernames, room names and file names are validated so nobody can write outside ./storage/IT23858916/<sender>/.
Rooms. Created by the first JOIN, never deleted (kept simple).

Obstacles and how I solved them
GitHub authentication failed from the VM. `git push` said password authentication is not supported. Fix: created a Personal Access Token on GitHub and used it as the password. It rejected my password, so I made a token.
"Nothing to commit". I tried to commit before editing README.md. Fix: edit the file first, then add, commit and push.
GitHub website upload stuck / "File could not be edited". Fixed by using a private (Incognito) browser window. The upload hung, so I used a private window
Makefile needs real tab characters. Uploaded the original file instead of pasting so the tabs stayed intact; confirmed with make.

Testing notes
Tested with three clients at once (Kavindu, Dilshan, Fernando): LIST, BCAST, PMSG, JOIN/ROOMS/RMSG/LEAVE, SENDFILE, error cases, a client killed with Ctrl+C, and QUIT. Screenshots are in the report.

What I would improve with more time
Avoid holding the mutex while forwarding large files.
Add an optional extension (for example rate limiting or a simple token login).
Use a buffered line reader instead of one-byte recv().
