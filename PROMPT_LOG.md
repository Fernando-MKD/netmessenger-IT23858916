Prompt Log - AI use for Part 1 (IE3010, IT23858916)
Tool used: Claude (Anthropic), chat interface. Used for planning, GitHub help, generating the first draft of the C code, and drafting the report/diary templates.

#	Stage	Prompt (summary)	How the output was used or changed
1	Planning	Uploaded the assignment PDF, asked for a step-by-step plan as a sysadmin/network engineer with no GitHub experience.	Used as a roadmap. It pointed out the deadline conflict between the PDF (30 Sept) and CourseWeb (7 Oct) and the different file-naming rules; I followed CourseWeb.
2	GitHub setup	Asked how to create the repo, token, and push from a CentOS 10 VM. pasted my git errors (nothing to commit, authentication failed).	Followed the steps: created a Personal Access Token and fixed the commit order.
3	Code	Asked Claude guide me to write server/client in C.	Claude guide me to write the thread-per-client server (about 285 lines) and a 91-line client. Claude also compiled and ran its own automated tests on Linux. I then built it in my VM (no warnings), ran my own manual tests with three clients, and read the code.
4	Git workflow	Asked about the .gitignore and about GitHub web upload errors.	Used an Incognito window to fix the upload error.

What I verified myself
Compiled with gcc -Wall -Wextra with no warnings in my VM
Ran each test in the testing table and took my own screenshots
Practised changing the code without AI before the lab

Things AI got wrong or I changed
The first code was he guide me to write, it was too long for me to explain, so I rejected it.
