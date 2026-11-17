# frozen_string_literal: true

# Fetches the downloads deps.lock pins (Freedoom's release archive) over HTTPS
# into third_party/ (not checked in), verifies each one's SHA-256 and unpacks
# it. Nothing fetched is run. doomgeneric itself is this repository's
# doomgeneric/, and erXos's `tools/erx fetch --example doom` reads the same
# deps.lock, so this script is only for building outside an erXos tree.
#
#   ruby fetch.rb

require 'digest'
require 'fileutils'
require 'json'

module ErxDoomFetch
  Source = Data.define(:id, :kind, :url, :sha256)

  LOCK = File.join(__dir__, 'deps.lock')
  DIR = File.join(__dir__, 'third_party')

  module_function

  def sources
    JSON.parse(File.read(LOCK)).fetch('sources').map do |row|
      Source.new(id: row.fetch('id'), kind: row.fetch('kind'), url: row.fetch('url'), sha256: row.fetch('sha256'))
    end
  end

  def fetch(source)
    raise "#{source.id}: kind #{source.kind}, only zip is unpacked" unless source.kind == 'zip'

    path = File.join(DIR, File.basename(source.url))
    unless File.exist?(path) && Digest::SHA256.file(path).hexdigest == source.sha256
      system('curl', '-fsSL', '--proto', '=https', '-o', path, source.url, exception: true)
    end
    got = Digest::SHA256.file(path).hexdigest
    raise "#{source.id}: SHA-256 #{got}, pinned #{source.sha256}" unless got == source.sha256

    system('unzip', '-q', '-o', path, '-d', DIR, exception: true)
    puts "#{source.id}: verified and unpacked"
  end

  def call
    FileUtils.mkdir_p(DIR)
    sources.each { |source| fetch(source) }
  end
end

ErxDoomFetch.call
