MRuby::Gem::Specification.new('picoruby-midi') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Toshio Maki'
  spec.summary = 'MIDI protocol layer (parser, scheduler, clock) for PicoRuby'
  spec.require_name = 'midi'
  spec.add_dependency 'picoruby-machine'

  # MIDI::Filter (named filters) takes keyword splats and class-level
  # declarations, which mruby/c does not have: mruby only
  if spec.build.respond_to?(:vm_mrubyc?) && spec.build.vm_mrubyc?
    spec.rbfiles.delete("#{spec.dir}/mrblib/midi_filter_class.rb")
  end
end
