#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is a preview server for the apps and extensions docs. Navigate to a docs
# page, and the page will be rendered how it will be on the production server.
#
# For example: http://localhost:8000/tabs.html will render the docs page for
# the tabs API.
#
# Run with: './preview.py'

from BaseHTTPServer import BaseHTTPRequestHandler, HTTPServer
import optparse
import os
import shutil
from StringIO import StringIO
import sys
import urlparse

import build_server
# Copy all the files necessary to run the server. These are cleaned up when the
# server quits.
build_server.main()
from handler import Handler

class Response(object):
  def __init__(self):
    self.status = 200
    self.out = StringIO()
    self.headers = {}

  def set_status(self, status):
    self.status = status

class Request(object):
  def __init__(self, path):
    self.headers = {}
    self.path = path

def _GetLocalPath():
  return os.path.join(sys.argv[0].rsplit('/', 1)[0], os.pardir, os.pardir)

class RequestHandler(BaseHTTPRequestHandler):
  """A HTTPRequestHandler that outputs the docs page generated by Handler.
  """
  def do_GET(self):
    parsed_url = urlparse.urlparse(self.path)
    request = Request(parsed_url.path)
    response = Response()
    Handler(request, response, local_path=_GetLocalPath()).get()
    content = response.out.getvalue()
    if isinstance(content, str):
      self.wfile.write(content)
    else:
      self.wfile.write(content.encode('utf-8', 'replace'))

if __name__ == '__main__':
  parser = optparse.OptionParser(
      description='Runs a server to preview the extension documentation.',
      usage='usage: %prog [option]...')
  parser.add_option('-p', '--port', default="8000",
      help='port to run the server on')

  (opts, argv) = parser.parse_args()

  print('Starting previewserver on port %s' % opts.port)
  print('The extension documentation can be found at:')
  print('')
  print('  http://localhost:%s' % opts.port)
  print('')

  server = HTTPServer(('', int(opts.port)), RequestHandler)
  try:
    server.serve_forever()
  finally:
    server.socket.close()
    shutil.rmtree(os.path.join(sys.argv[0].rsplit(os.sep, 1)[0], 'third_party'))
