---
title: Installing
---

## Installing

Getting started is easy. Install AppScope, then explore its CLI.

### Get AppScope

You can download as a binary to run on your Linux OS, or as a container.

#### Download as Binary

First, see [Requirements](/docs/requirements) to ensure that you’re completing these steps on a supported system. 

Next, use these commands to download the CLI binary and make it executable:

```
LATEST=$(curl -Ls https://cdn.cribl.io/dl/scope/latest)
curl -Lo scope https://cdn.cribl.io/dl/scope/$LATEST/linux/$(uname -m)/scope
curl -Ls https://cdn.cribl.io/dl/scope/$LATEST/linux/$(uname -m)/scope.md5 | md5sum -c 
chmod +x scope
```

That's it!

To verify installation, run some scope commands:

```
scope run ps -ef
scope events
scope metrics
```

#### Download as Container

Visit the AppScope repo on Docker Hub to download and run the most recently tagged container:

https://hub.docker.com/r/cribl/scope

The container provides the AppScope binary on Ubuntu 20.04.

### Explore the CLI

- Run `scope --help` or `scope -h` to view CLI options.
- Try some basic CLI commands in [Using the CLI](/docs/quick-start-guide).
- See the complete [CLI Reference](/docs/cli-reference).
