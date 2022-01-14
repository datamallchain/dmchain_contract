# Building contracts

----------------------------------
## Download

```sh
git clone git_path --recursive
```


If a repository is cloned without the --recursive flag, the submodules can be retrieved after the fact by running this command from within the repo:
```sh
cd contracts
git submodule update --init --recursive
```


## Update

```sh
cd contracts
git pull
git submodule update --init --recursive
```


## Build

```sh
cd contracts
bash build.sh
```
