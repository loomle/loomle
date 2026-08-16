# frozen_string_literal: true

require "rouge"

module Rouge
  module Lexers
    # Syntax highlighting for Structured Agent Language.
    #
    # This is intentionally a lexer rather than a second SAL parser. Its job is
    # to make the language's operational structure legible while the TypeScript
    # parser remains the source of truth for syntax and semantics.
    class SAL < RegexLexer
      title "Structured Agent Language"
      desc "Compact text for human-agent-computer collaboration"
      tag "sal"
      filenames "*.sal"
      mimetypes "text/x-sal"

      DECLARATIONS = %w[
        query patch result
      ].freeze

      STRUCTURE = %w[
        target domain object related handoff objects no_objects
      ].freeze

      MUTATIONS = %w[
        add remove set reset move invoke save compile connect disconnect bind
        unbind break insert wrap replace
      ].freeze

      QUERY_OPERATIONS = %w[
        summary references exec data flow context tree palette entries assets
        variables dispatchers graphs components nodes properties functions
        defaults widgets states parameters variable dispatcher graph component
        property function default widget
      ].freeze

      CLAUSES = %w[
        dry run where with schema order by page limit after before to from in
        depth as and or not project
      ].freeze

      DOMAINS = %w[
        asset blueprint class graph state_tree widget level pcg pcg_component
      ].freeze

      RESULT_CONTEXTS = %w[
        exact_target domain_root unresolved_target
      ].freeze

      state :root do
        rule %r/^###[ \t]*$/, Comment::Multiline, :block_comment
        rule %r/#[^\n]*/, Comment::Single
        rule %r/\s+/, Text::Whitespace

        rule %r/"/, Str::Double, :string

        # Labels and declarations are recognized before general keywords so
        # native field names such as `domain:` stay visibly data-shaped.
        rule %r/[A-Za-z_][A-Za-z0-9_]*(?=[ \t]*:)/, Name::Label
        rule %r/[A-Za-z_][A-Za-z0-9_]*(?=[ \t]*=)/, Name::Variable

        rule %r/\b(?:#{Regexp.union(DECLARATIONS).source})\b/, Keyword::Declaration
        rule %r/\b(?:#{Regexp.union(STRUCTURE).source})\b/, Keyword::Reserved
        rule %r/\b(?:#{Regexp.union(MUTATIONS).source})\b/, Keyword
        rule %r/\b(?:#{Regexp.union(QUERY_OPERATIONS).source})\b/, Name::Builtin
        rule %r/\b(?:#{Regexp.union(CLAUSES).source})\b/, Keyword::Pseudo
        rule %r/\b(?:#{Regexp.union(DOMAINS).source})\b/, Keyword::Type
        rule %r/\b(?:#{Regexp.union(RESULT_CONTEXTS).source})\b/, Name::Constant
        rule %r/\b(?:true|false|null)\b/, Keyword::Constant

        # Stable references retain their native identity while `/` remains a
        # visible relationship operator. Tagged references and semantic object
        # tags receive the same structural treatment as Domain terms.
        rule %r/[A-Za-z_][A-Za-z0-9_]*(?=[ \t]+(?:[A-Za-z_][A-Za-z0-9_]*::)?@)/, Name::Class
        rule %r/[A-Za-z_][A-Za-z0-9_]*(?=[ \t]*\{)/, Name::Class
        rule %r/(?:[A-Za-z_][A-Za-z0-9_]*::)?@[-A-Za-z0-9_]+/, Name::Namespace
        rule %r/(\/)([-A-Za-z0-9_]+)/ do
          groups Operator, Name::Namespace
        end
        rule %r/(\.)([A-Za-z_][A-Za-z0-9_]*)/ do
          groups Operator, Name::Attribute
        end
        rule %r/[A-Za-z_][A-Za-z0-9_]*(?=[ \t]*\()/, Name::Function

        rule %r/-?(?:0|[1-9]\d*)\.\d+(?:e[+-]?\d+)?/i, Num::Float
        rule %r/-?(?:0|[1-9]\d*)(?:e[+-]?\d+)?/i, Num::Integer
        rule %r/\b[A-Z][A-Za-z0-9_]*\b/, Name::Constant

        rule %r/->|~=|==|!=|>=|<=|>|<|=|\/|\.|::/, Operator
        rule %r/[{}\[\](),:]/, Punctuation
        rule %r/[A-Za-z_][A-Za-z0-9_]*/, Name
        rule %r/./, Text
      end

      state :string do
        rule %r/[^\\"]+/, Str::Double
        rule %r/\\./, Str::Escape
        rule %r/"/, Str::Double, :pop!
      end

      state :block_comment do
        rule %r/^###[ \t]*$/, Comment::Multiline, :pop!
        rule %r/[^\n]+/, Comment::Multiline
        rule %r/\n/, Comment::Multiline
      end
    end
  end
end
