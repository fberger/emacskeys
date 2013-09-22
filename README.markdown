EmacsKeys
=========

EmacsKeys is a plugin that brings Emacs key bindings and functionality to the
Qt Creator IDE.

Latest Updates
==============

For a version that works with version 2.5.2 of Qt Creator, see repository: https://github.com/mrjames313/emacskeys.

Features
========

EmacsKeys provides the following features:

* EmacsKeys.kms - A Keyboard Mapping Scheme for Qt Creator that can be
imported in Options -> Environment -> Keyboard. It overrides some of the
standard key bindings used in Qt Creator and replaces them with Emacs
ones: C-s, C-x,s, C-x,C-s, C-x,C-w.

* Kill ring - the Emacs kill ring allows you to maintain a history of your
clipboards content. Caveat: It only works when text is inserted into it with
C-W, M-w, C-k, M-d and M-Backspace.

* The following keys work as expected: C-n, C-p, C-a, C-e, C-b, C-f, M-b, M-f,
  M-d, M-Backspace, C-d, M-<, M->, C-v, M-v, C-Space, C-k, C-y, M-y, C-w, M-w,
  C-l, C-@.

* C-x,b opens the quick open dialog at the bottom left.

* C-x,C-b switches to the File System view on the left.

* M-/ triggers the code completion that is triggered by C-Space normally.

* Mnemonics are removed from some of the menus to allow conflicting Emacs keys
  to work.

Build Instructions
==================

* Download the source of Qt Creator and and checkout the branch with the respective version number. For instance, if you download Qt Creator v2.2.1, checkout branch v2.2.1.
* cd src/plugins/
* git clone git://github.com/fberger/emacskeys.git
* git checkout -b local [origin/v2.2.1|origin/v2.0.1]
* patch -p 3 < emacskeys/plugins.pro.patch
* cd ../../
* qmake && make
* bin/qtcreator
* Load EmacsKeys.kms from Options -> Environment -> Keyboard
* Activate EmacsKeys Plugin

Credit
======

The Emacs Key code is based on FakeVim, (C) 2008-2009 Nokia Corporation.

License
=======

The code is licensed under the LGPL version 2.1.

