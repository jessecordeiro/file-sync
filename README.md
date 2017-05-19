# file-sync

file-sync is a command-line program intended to simplify transmitting files across a client and server.

file-sync is an MIT open-source project.

## Contents

* [Requirements](#requirements)
* [Quick Start](#quick-start)
* [Usage](#usage)
* [Contribute](#contribute)
* [License](https://github.com/jessecordeiro/file-sync/blob/master/LICENSE)

## <a name="requirements"></a>Requirements

  + Unix environment
  + Experience with a CLI

## <a name="quick-start"></a>Quick Start

Follow the steps below to get started with file-sync in seconds

1. **Clone repository:**

  ```bash
  $ git clone https://github.com/jessecordeiro/file-sync.git
  $ cd file-sync
  ```

2. **Run automated build tasks:**

  ```bash
  $ make
  ```

## <a name="usage"></a>Usage
1. **Server:**
  ```bash
  ./rcopy_server PATH_PREFIX
         PATH_PREFIX - The absolute path on the server that is used as the path prefix
            for the destination in which to copy files and directories.
  ```
2. **Client:**
  ```bash
  ./rcopy_client SRC HOST
         SRC - The file or directory to copy to the server
         HOST - The hostname of the server
  ```

## <a name="contribute"></a>Contribute
1. Fork the repository
2. Create your feature branch: `git checkout -b my-new-feature`
3. Commit your changes: `git commit -am 'Implemented x feature'`
4. Push to the feature branch: `git push origin x-feature`
5. Open a pull request
