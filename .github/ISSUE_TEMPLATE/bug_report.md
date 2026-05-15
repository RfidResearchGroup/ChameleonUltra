---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

***Things to try before submitting a bug report***

* read the [troubleshooting guide](https://github.com/RfidResearchGroup/ChameleonUltra/blob/main/docs/troubleshooting.md)
* check existing [issues](https://github.com/RfidResearchGroup/ChameleonUltra/issues)
* use the latest firmware and CLI
* for issues specific to another client than the Python CLI, use the corresponding issue tracker. E.g. [here](https://github.com/GameTec-live/ChameleonUltraGUI/issues) for the ChameleonUltraGUI

***Compilation problems***
Try compiling with verbose. Use `make VERBOSE=1` for the firmware and the software/src tools.
Include the verbose compilation logs.

***flashing problems***
Have you followed the instructions properly?


**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior.  
Explain it as you would do to someone not familiar with the problematic feature.  
What is the abnormal behavior you observed?  
E.g.
1. Connect '....'
2. Execute '....'
3. Press button '....'
4. See error '....'

**Expected behavior**
A clear and concise description of what you expected to happen.

**Screenshots**
If applicable, add screenshots to help explain your problem. For console text and logs, better to dump them as text than image. Attach files if too long.

**Host (please complete the following information):**
 - OS and version
 - for compilation issues, the toolchain version
 - inside CLI run `hw version` and paste the output here
 
**Additional context**
Add any other context about the problem here.
